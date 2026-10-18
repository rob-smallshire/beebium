# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""MODE 7 screen-reading helpers for the beebium client.

These read the screen as it is displayed: the cells come from the server's
teletext grid (``video.teletext_screen()``), which the SAA5050 fills from the
bytes the video circuitry fetches. So they follow hardware scrolling, and in a
shadow mode they read the RAM on screen -- the B+'s shadow RAM, or the
Integra-B's main RAM while the CPU sees its extra RAM at 0x3000 -- not what
the CPU sees at 0x7C00.

The functions form a small composable library:

* ``cells_to_text`` / ``linearise`` are pure transforms (no emulator required)
  over the 25x40 grid and a flattened view respectively.
* ``read_mode7_cells`` / ``read_mode7_screen`` / ``screen_contains`` / ``find``
  / ``dump_screen`` compose those transforms onto a running client.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import TYPE_CHECKING, Protocol, TypeVar, cast

from beebium.client.exceptions import BeebiumError

if TYPE_CHECKING:
    from beebium.client.video import Video

# A row is 40 raw screen bytes; rows compose into the 25-row display grid.
Row = bytes | str

# Row-polymorphic helpers preserve the row type: given byte rows they return
# bytes, given text rows they return str.
RowT = TypeVar("RowT", bytes, str)

# A separator strategy decides what (if anything) to insert between two adjacent
# rows when flattening the grid. It receives the two full-width rows and returns
# a separator of the same type (bytes for byte rows, str for text rows), so that
# decisions based on the last/first cell of each row are possible.
Separator = Callable[[Row, Row], Row]


class _ScreenClient(Protocol):
    """The slice of the Beebium client these helpers need.

    Declared as read-only properties (not plain attributes) so the client's
    ``@property`` accessors satisfy the protocol -- a read-only property is not
    assignable to a writable protocol attribute.
    """

    @property
    def video(self) -> Video: ...


# ---------------------------------------------------------------------------
# Pure transforms
# ---------------------------------------------------------------------------


def cells_to_text(cells: list[bytes]) -> list[str]:
    """Convert raw grid rows to printable text rows.

    Each byte is masked to 7 bits (the SAA5050 ignores bit 7, so e.g. 0xC1
    displays as 'A'). Printable ASCII (0x20-0x7E) is kept; teletext control
    codes (0x00-0x1F) and 0x7F become spaces.

    Args:
        cells: Rows of raw screen bytes.

    Returns:
        One text string per row.
    """
    text_rows: list[str] = []
    for row in cells:
        chars = []
        for byte in row:
            code = byte & 0x7F
            chars.append(chr(code) if 0x20 <= code <= 0x7E else " ")
        text_rows.append("".join(chars))
    return text_rows


def _is_letter(cell: int | str) -> bool:
    code = cell if isinstance(cell, int) else ord(cell)
    return 0x41 <= code <= 0x5A or 0x61 <= code <= 0x7A


def _like(template: RowT, byte_value: bytes, str_value: str) -> RowT:
    """Return byte_value or str_value, matching the row type of template.

    The isinstance check guarantees the correct branch; the cast tells the type
    checker what it cannot infer -- that the result matches the template type.
    """
    return cast(RowT, byte_value if isinstance(template, (bytes, bytearray)) else str_value)


def _blank(like: RowT) -> RowT:
    return _like(like, b"", "")


def _space(like: RowT) -> RowT:
    return _like(like, b" ", " ")


def no_separator(left: RowT, right: RowT) -> RowT:
    """Separator strategy: never insert anything between rows."""
    return _blank(left)


def spaced(left: RowT, right: RowT) -> RowT:
    """Separator strategy: always insert a single space between rows."""
    return _space(left)


def lined(left: RowT, right: RowT) -> RowT:
    """Separator strategy: insert a newline between rows."""
    return _like(left, b"\n", "\n")


def dewrapped(left: RowT, right: RowT) -> RowT:
    """Separator strategy that reassembles character-wrapped words.

    The BBC's default character wrap can split a word at the 40-column
    boundary, leaving the line full (a letter in the last column) and the word
    continuing in the first column of the next line. In that case no separator
    is inserted, so the word is rejoined. Otherwise a space is inserted.

    Note the inherent ambiguity: a character-wrapped line that happens to fill
    to column 40 ending a complete word, followed by a new word, is
    indistinguishable from a split word and will be joined without a space.
    Most application text is word-wrapped (the last column is a space), where
    this does not arise.
    """
    if left and right and _is_letter(left[-1]) and _is_letter(right[0]):
        return _blank(left)
    return _space(left)


def linearise(rows: list[RowT], separator: Callable[[RowT, RowT], RowT] = spaced) -> RowT:
    """Flatten grid rows into a single sequence for searching or display.

    Trailing padding is stripped from each row; ``separator`` decides what to
    place between adjacent rows (it sees the full, unstripped rows).

    Args:
        rows: Grid rows, all ``bytes`` or all ``str``.
        separator: A separator strategy (e.g. ``spaced``, ``lined``,
            ``no_separator``, ``dewrapped``).

    Returns:
        The flattened ``bytes`` or ``str`` (matching the row type).
    """
    if not rows:
        # No rows to sample the type from; the historical fallback is an empty
        # string.
        return cast(RowT, "")
    parts: list[RowT] = []
    last = len(rows) - 1
    for index, row in enumerate(rows):
        parts.append(row.rstrip())
        if index < last:
            parts.append(separator(row, rows[index + 1]))
    # The parts are homogeneous (all the row type), but the type checker cannot
    # prove join() preserves that, so branch on the concrete type and assert it.
    if isinstance(rows[0], bytes):
        return cast(RowT, b"".join(cast("list[bytes]", parts)))
    return cast(RowT, "".join(cast("list[str]", parts)))


# ---------------------------------------------------------------------------
# Composition layer (operates on a running client)
# ---------------------------------------------------------------------------


def _displayed_cells(bbc: _ScreenClient) -> list[bytes] | None:
    """The displayed MODE 7 grid as rows of character codes, or None when the
    display is not MODE 7 (or has shown no MODE 7 frame yet)."""
    screen = bbc.video.teletext_screen()
    if not screen.active:
        return None
    return [
        bytes(screen.cell(row, column).character for column in range(screen.columns))
        for row in range(screen.rows)
    ]


def _displayed_rows(bbc: _ScreenClient) -> list[str] | None:
    """The displayed MODE 7 grid as text rows, or None when the display is
    not MODE 7."""
    cells = _displayed_cells(bbc)
    return None if cells is None else cells_to_text(cells)


def read_mode7_cells(bbc: _ScreenClient) -> list[bytes]:
    """Read the displayed MODE 7 screen as a 25x40 grid of character codes.

    The grid is the one the SAA5050 displayed in the last complete frame, so
    it is read in one piece, follows hardware scrolling, and in a shadow mode
    is the RAM on screen. Each code is the 7 bits the SAA5050 uses; bit 7 of
    the screen byte is not kept.

    Args:
        bbc: A Beebium client.

    Returns:
        25 rows of 40 character codes each, in display order.

    Raises:
        BeebiumError: If the display is not MODE 7.
    """
    cells = _displayed_cells(bbc)
    if cells is None:
        raise BeebiumError("the display is not MODE 7, so there is no MODE 7 screen to read")
    return cells


def read_mode7_screen(bbc: _ScreenClient) -> list[str]:
    """Read the displayed MODE 7 screen as 25 rows of 40 characters.

    Args:
        bbc: A Beebium client.

    Returns:
        A list of 25 strings, each 40 characters long.

    Raises:
        BeebiumError: If the display is not MODE 7.
    """
    return cells_to_text(read_mode7_cells(bbc))


def screen_contains(bbc: _ScreenClient, text: str) -> bool:
    """Return whether ``text`` appears anywhere on the MODE 7 display.

    The display is flattened with the ``dewrapped`` strategy, so text is found
    whether or not it has been wrapped across a line boundary. A display that
    is not MODE 7 contains nothing, so this can poll a machine that is still
    changing mode.

    Args:
        bbc: A Beebium client.
        text: The text to search for.

    Returns:
        True if the text is present on screen.
    """
    rows = _displayed_rows(bbc)
    return rows is not None and text in linearise(rows, dewrapped)


def find(bbc: _ScreenClient, text: str) -> tuple[int, int] | None:
    """Locate ``text`` within a single row of the MODE 7 display.

    Searches each row independently and returns the first match. Text that
    spans a row boundary is not located here; use ``screen_contains`` to test
    for presence anywhere on screen. A display that is not MODE 7 holds no
    match.

    Args:
        bbc: A Beebium client.
        text: The text to locate.

    Returns:
        ``(row, column)`` of the first occurrence, or ``None`` if not found.
    """
    for row, line in enumerate(_displayed_rows(bbc) or []):
        column = line.find(text)
        if column != -1:
            return (row, column)
    return None


def dump_screen(bbc: _ScreenClient) -> str:
    """Dump the MODE 7 display as a formatted string for diagnostics.

    Args:
        bbc: A Beebium client.

    Returns:
        A multi-line string with row numbers and the screen content, or a
        line saying the display is not MODE 7.
    """
    rows = _displayed_rows(bbc)
    if rows is None:
        return "MODE 7 screen: the display is not MODE 7"
    lines = ["MODE 7 screen:"]
    for index, row in enumerate(rows):
        lines.append(f"Row {index:2d}: [{row}]")
    return "\n".join(lines)

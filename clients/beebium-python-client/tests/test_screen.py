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

"""Unit tests for the MODE 7 screen-reading helpers.

These exercise the pure transforms (no emulator required) and the small
composition layer that wires them onto a Beebium client. A lightweight fake
stands in for the client so the composition logic can be tested offline; the
real end-to-end behaviour, shadow modes included, is covered by
test_screen_shadow_modes.py.
"""

from __future__ import annotations

from types import SimpleNamespace

from beebium.client.screen import (
    cells_to_text,
    dewrapped,
    dump_screen,
    find,
    linearise,
    lined,
    no_separator,
    read_mode7_cells,
    read_mode7_screen,
    screen_contains,
    spaced,
)
from beebium.client.video import TeletextScreen

ROWS = 25
COLUMNS = 40


# ---------------------------------------------------------------------------
# Fake client for the composition layer
# ---------------------------------------------------------------------------


class _Video:
    def __init__(self, screen: TeletextScreen) -> None:
        self._screen = screen

    def teletext_screen(self) -> TeletextScreen:
        return self._screen


class _FakeBbc:
    def __init__(self, screen: TeletextScreen) -> None:
        self.video = _Video(screen)


def _bbc_showing(lines: list[str], *, active: bool = True) -> _FakeBbc:
    """Build a fake client whose displayed teletext grid renders ``lines``
    at the top of the screen."""
    codes = [ord(" ")] * (ROWS * COLUMNS)
    for row, line in enumerate(lines):
        for column, ch in enumerate(line):
            codes[row * COLUMNS + column] = ord(ch)
    return _FakeBbc(
        TeletextScreen(
            active=active,
            rows=ROWS,
            columns=COLUMNS,
            text="",
            frame_number=1,
            cells=[SimpleNamespace(character=code) for code in codes],
        )
    )


# ---------------------------------------------------------------------------
# cells_to_text: raw bytes -> printable text
# ---------------------------------------------------------------------------


class TestCellsToText:
    def test_printable_ascii_passes_through(self) -> None:
        (text,) = cells_to_text([b"Hello, World! ~"])
        assert text == "Hello, World! ~"

    def test_control_codes_and_del_become_spaces(self) -> None:
        (text,) = cells_to_text([bytes([0x00, 0x1F, 0x7F, ord("Z")])])
        assert text == "   Z"

    def test_bit7_is_masked_so_high_bytes_still_render(self) -> None:
        # The SAA5050 ignores bit 7; 0xC1 displays as 'A'.
        (text,) = cells_to_text([bytes([0xC1, 0x80 | ord("z")])])
        assert text == "Az"

    def test_one_string_per_row(self) -> None:
        rows = cells_to_text([b"ab", b"cd", b"ef"])
        assert rows == ["ab", "cd", "ef"]


# ---------------------------------------------------------------------------
# Separator strategies
# ---------------------------------------------------------------------------


class TestSeparators:
    def test_constants_for_str(self) -> None:
        assert no_separator("a", "b") == ""
        assert spaced("a", "b") == " "
        assert lined("a", "b") == "\n"

    def test_constants_for_bytes(self) -> None:
        assert no_separator(b"a", b"b") == b""
        assert spaced(b"a", b"b") == b" "
        assert lined(b"a", b"b") == b"\n"

    def test_dewrapped_joins_split_word(self) -> None:
        # A full row ending in a letter, next row beginning with a letter:
        # the word was split at the column-40 boundary.
        left = "x" * 36 + "moun"
        right = "tain" + " " * 36
        assert dewrapped(left, right) == ""

    def test_dewrapped_spaces_after_padded_line(self) -> None:
        # A line that ended early (col 39 is a space) is a real line break.
        left = "The" + " " * 37
        right = "walls" + " " * 35
        assert dewrapped(left, right) == " "

    def test_dewrapped_spaces_after_punctuation(self) -> None:
        # Filled to column 40 but ending in punctuation: not a split word.
        left = "x" * 39 + "."
        right = "You" + " " * 37
        assert dewrapped(left, right) == " "

    def test_dewrapped_matches_bytes_type(self) -> None:
        assert dewrapped(b"x" * 36 + b"moun", b"tain" + b" " * 36) == b""
        assert dewrapped(b"The" + b" " * 37, b"walls" + b" " * 35) == b" "


# ---------------------------------------------------------------------------
# linearise
# ---------------------------------------------------------------------------


class TestLinearise:
    def test_spaced_rstrips_padding(self) -> None:
        rows = ["The" + " " * 37, "walls" + " " * 35]
        assert linearise(rows, spaced) == "The walls"

    def test_no_separator_concatenates_content(self) -> None:
        rows = ["abc" + " " * 37, "def" + " " * 37]
        assert linearise(rows, no_separator) == "abcdef"

    def test_lined_joins_with_newline(self) -> None:
        rows = ["abc" + " " * 37, "def" + " " * 37]
        assert linearise(rows, lined) == "abc\ndef"

    def test_dewrapped_reassembles_wrapped_word(self) -> None:
        left = "x" * 36 + "moun"
        right = "tain" + " " * 36
        assert linearise([left, right], dewrapped).endswith("mountain")

    def test_returns_bytes_for_bytes_rows(self) -> None:
        rows = [b"abc" + b" " * 37, b"def" + b" " * 37]
        assert linearise(rows, no_separator) == b"abcdef"
        assert isinstance(linearise(rows, spaced), bytes)

    def test_single_row_has_no_separator(self) -> None:
        assert linearise(["only" + " " * 36], spaced) == "only"

    def test_empty_rows(self) -> None:
        assert linearise([], spaced) == ""


# ---------------------------------------------------------------------------
# Composition layer (read_mode7_cells, read_mode7_screen, screen_contains,
# find, dump_screen) via the fake client.
# ---------------------------------------------------------------------------


class TestComposition:
    def test_read_mode7_cells_returns_grid(self) -> None:
        bbc = _bbc_showing(["HELLO"])
        cells = read_mode7_cells(bbc)
        assert len(cells) == ROWS
        assert cells[0][:5] == b"HELLO"

    def test_read_mode7_screen(self) -> None:
        bbc = _bbc_showing(["HELLO", "WORLD"])
        rows = read_mode7_screen(bbc)
        assert rows[0].startswith("HELLO")
        assert rows[1].startswith("WORLD")
        assert len(rows) == ROWS
        assert all(len(row) == COLUMNS for row in rows)

    def test_a_display_that_is_not_mode_7_reads_blank(self) -> None:
        # So a polling loop can read the screen from launch, before the first
        # MODE 7 frame has been displayed.
        bbc = _bbc_showing(["STALE"], active=False)
        assert read_mode7_cells(bbc) == [b" " * COLUMNS] * ROWS
        assert read_mode7_screen(bbc) == [" " * COLUMNS] * ROWS

    def test_a_display_that_is_not_mode_7_contains_nothing(self) -> None:
        bbc = _bbc_showing(["STALE"], active=False)
        assert not screen_contains(bbc, "STALE")
        assert find(bbc, "STALE") is None
        assert dump_screen(bbc) == "MODE 7 screen: the display is not MODE 7 (frame 1)"

    def test_screen_contains_finds_text(self) -> None:
        bbc = _bbc_showing(["You are on the top of a mountain."])
        assert screen_contains(bbc, "top of a mountain")
        assert not screen_contains(bbc, "dungeon")

    def test_screen_contains_reassembles_char_wrapped_word(self) -> None:
        bbc = _bbc_showing(["x" * 36 + "moun", "tain and more"])
        assert screen_contains(bbc, "mountain")

    def test_find_returns_row_and_column(self) -> None:
        bbc = _bbc_showing(["", "  HELLO"])
        assert find(bbc, "HELLO") == (1, 2)

    def test_find_returns_none_when_absent(self) -> None:
        bbc = _bbc_showing(["HELLO"])
        assert find(bbc, "GOODBYE") is None

    def test_dump_screen_includes_rows(self) -> None:
        bbc = _bbc_showing(["HELLO"])
        dump = dump_screen(bbc)
        assert "HELLO" in dump
        assert dump.count("Row ") == ROWS

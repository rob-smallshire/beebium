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

"""Reading the MODE 7 screen as characters.

The cells are captured inside the SAA5050 after control codes are resolved, so
unlike reading screen memory there is no hardware-scroll offset to undo and no
attribute state to re-derive. See docs/discussion/teletext-cell-capture.md.
"""

from __future__ import annotations

from beebium.client import Beebium


def _wait_for_prompt(bbc: Beebium) -> None:
    bbc.run_until_or_timeout(
        lambda: ">" in bbc.video.teletext_screen().text, emulated_seconds=5.0
    )


class TestTeletextScreen:
    """The whole screen."""

    def test_boot_screen_reads_back_as_text(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        screen = bbc.video.teletext_screen()

        assert screen.active
        assert screen.rows == 25
        assert screen.columns == 40
        assert len(screen.cells) == 25 * 40
        assert "BBC Computer" in screen.text
        assert "BASIC" in screen.text

    def test_typed_text_appears(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        bbc.keyboard.type('PRINT "TELETEXT"\r')

        # Advance the machine while waiting rather than calling
        # wait_until_typing_complete(): run_until_or_timeout leaves the machine
        # stopped between chunks, and a stopped machine never drains the
        # type-ahead queue, so that wait would never return.
        found = bbc.run_until_or_timeout(
            lambda: "TELETEXT" in bbc.video.teletext_screen().text,
            emulated_seconds=10.0,
        )
        assert found

    def test_the_frame_number_advances(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        first = bbc.video.teletext_screen().frame_number
        bbc.run_until_or_timeout(lambda: False, emulated_seconds=0.5)
        assert bbc.video.teletext_screen().frame_number > first

    def test_lines_are_joined_with_lf(self, bbc: Beebium) -> None:
        # The wire carries one canonical form; converting to a platform-native
        # ending is the client's business, at the point text meets a clipboard.
        _wait_for_prompt(bbc)
        text = bbc.video.teletext_screen().text
        assert "\r" not in text
        assert "\n" in text

    def test_no_trailing_blank_lines(self, bbc: Beebium) -> None:
        # A screen is 25 rows whatever is written on it.
        _wait_for_prompt(bbc)
        text = bbc.video.teletext_screen().text
        assert text == text.rstrip("\n")


class TestTeletextRegion:
    """Reading part of the screen, as a dragged selection will."""

    def test_a_region_returns_only_its_own_cells(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        region = bbc.video.teletext_screen(row=0, column=0, rows=4, columns=10)

        assert region.rows == 4
        assert region.columns == 10
        assert len(region.cells) == 40

    def test_a_region_is_clipped_to_the_screen(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        region = bbc.video.teletext_screen(row=20, column=30, rows=100, columns=100)

        assert region.rows == 5
        assert region.columns == 10

    def test_a_region_of_the_banner_reads_as_that_text(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        whole = bbc.video.teletext_screen()
        banner_row = next(
            row
            for row, line in enumerate(whole.text.split("\n"))
            if "BBC Computer" in line
        )

        region = bbc.video.teletext_screen(row=banner_row, rows=1)
        assert "BBC Computer" in region.text
        assert region.rows == 1


class TestTeletextCells:
    """The cells themselves, which drag-select will read."""

    def test_cells_carry_resolved_attributes(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        screen = bbc.video.teletext_screen()

        # Every cell has a foreground colour in range and a known character set.
        for cell in screen.cells[:200]:
            assert 0 <= cell.fg <= 7
            assert 0 <= cell.bg <= 7
            assert cell.character < 0x80

    def test_cell_lookup_matches_the_text(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        screen = bbc.video.teletext_screen()

        lines = screen.text.split("\n")
        banner_row = next(
            row for row, line in enumerate(lines) if "BBC Computer" in line
        )
        column = lines[banner_row].index("BBC Computer")

        assert chr(screen.cell(banner_row, column).character) == "B"


# Character-set codes as they arrive on the wire (proto TeletextCharacterSet).
CHARSET_ALPHA = 0
CHARSET_CONTIGUOUS = 1
CHARSET_SEPARATED = 2


class TestTeletextCellAttributes:
    """The per-cell attributes the SAA5050 resolves.

    These are the reason to read the grid rather than screen memory: colour,
    character set, concealment, flash and double height are carried on the cell,
    already resolved, so nothing downstream re-runs the serial control codes.
    Every attribute is exercised end to end by driving MODE 7 with the BASIC
    control codes that set it -- CHR$(129..159) map to the SAA5050's 0x01..0x1F
    after the top bit is masked. The core capture logic is pinned deterministically
    in tests/test_teletext_grid.cpp; here we prove the wire carries it.
    """

    def _find_cell(self, bbc: Beebium, predicate, seconds: float = 10.0):
        """Advance the machine until a cell satisfies predicate, and return it."""
        found = {}

        def hit() -> bool:
            for cell in bbc.video.teletext_screen().cells:
                if predicate(cell):
                    found["cell"] = cell
                    return True
            return False

        assert bbc.run_until_or_timeout(hit, emulated_seconds=seconds), (
            "no cell matched the attribute predicate"
        )
        return found["cell"]

    def test_a_colour_code_sets_the_foreground_and_is_marked_a_control_code(
        self, bbc: Beebium
    ) -> None:
        _wait_for_prompt(bbc)
        bbc.keyboard.type('PRINT CHR$(130);"Q"\r')  # alpha green, then Q

        cell = self._find_cell(bbc, lambda c: c.character == ord("Q") and c.fg == 2)
        assert cell.fg == 2
        assert not cell.is_control_code

        # The colour code itself occupies a cell, flagged and holding its column.
        control = self._find_cell(
            bbc, lambda c: c.is_control_code and c.character == 0x02
        )
        assert control.is_control_code

    def test_conceal_is_carried_even_though_the_text_hides_it(
        self, bbc: Beebium
    ) -> None:
        # A concealed cell keeps its character on the wire -- the reader is what
        # declines to copy it -- so it is found by cell, not by text.
        _wait_for_prompt(bbc)
        bbc.keyboard.type('PRINT CHR$(152);"Q"\r')  # conceal, then Q

        cell = self._find_cell(
            bbc, lambda c: c.character == ord("Q") and c.concealed
        )
        assert cell.concealed

    def test_graphics_codes_switch_the_character_set(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        # Graphics white then contiguous, and separately separated graphics.
        bbc.keyboard.type("PRINT CHR$(151);CHR$(255)\r")
        contiguous = self._find_cell(
            bbc, lambda c: c.charset == CHARSET_CONTIGUOUS
        )
        assert contiguous.charset == CHARSET_CONTIGUOUS

        bbc.keyboard.type("PRINT CHR$(151);CHR$(154);CHR$(255)\r")
        separated = self._find_cell(
            bbc, lambda c: c.charset == CHARSET_SEPARATED
        )
        assert separated.charset == CHARSET_SEPARATED

    def test_double_height_is_carried(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        bbc.keyboard.type('PRINT CHR$(141);"Q"\r')  # double height, then Q

        cell = self._find_cell(
            bbc, lambda c: c.character == ord("Q") and c.double_height_top
        )
        assert cell.double_height_top
        assert not cell.double_height_bottom

    def test_flash_is_carried_on_the_concealed_phase(self, bbc: Beebium) -> None:
        # Flash is phase-dependent: the cell reads as flashing only on the frames
        # where the character is hidden. Sampling across frames catches one.
        _wait_for_prompt(bbc)
        bbc.keyboard.type('PRINT CHR$(136);"Q"\r')  # flash, then Q

        cell = self._find_cell(
            bbc, lambda c: c.character == ord("Q") and c.flashing
        )
        assert cell.flashing

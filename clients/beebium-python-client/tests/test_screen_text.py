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

"""Reading text off the display, whatever mode is producing it.

The caller selects in pixels and the server picks a reading strategy per band
of scanlines, so nothing here mentions a mode except to set one up. See
docs/discussion/screen-text-extraction.md.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium


def _wait_for_prompt(bbc: Beebium) -> None:
    bbc.run_until_or_timeout(
        lambda: ">" in bbc.video.screen_text().text, emulated_seconds=5.0
    )


# The SAA5050 draws a character sixteen framebuffer pixels wide (two batches of
# eight); every bitmap mode uses eight. That makes the cell width the reliable
# sign of which is driving.
TELETEXT_CELL_WIDTH = 16


def _switch_to_mode(bbc: Beebium, mode: int, message: str) -> bool:
    """Leave the machine in a bitmap mode with something printed on it.

    Returns whether the mode change took, which is waited on through the
    geometry rather than through the text API: whether that API can read a
    bitmap mode is the very thing under test here.

    The wait keys on the cell width rather than the row pitch. A frame captured
    while the CRTC is being reprogrammed is briefly shorter than a whole
    screen, so the pitch derived from it dips before the mode has changed and a
    wait on the pitch returns while the display is still teletext.
    """
    _wait_for_prompt(bbc)
    bbc.keyboard.type(f'MODE {mode}\rPRINT "{message}"\r')

    # A stopped machine never drains the type-ahead queue, so the machine has
    # to be advanced while waiting rather than merely waited on.
    return bbc.run_until_or_timeout(
        lambda: bbc.video.screen_geometry().bands[0].cell_width
        != TELETEXT_CELL_WIDTH,
        emulated_seconds=10.0,
    )


def _run_program(bbc: Beebium, lines: list[str]) -> bool:
    """Enter and RUN a short BASIC program, leaving a bitmap mode on screen.

    The program's first line switches mode, so the lines the user typed are
    cleared as it runs and only what the program itself drew is left -- which
    is what lets a test tell grid text from off-grid text without the echoed
    source getting in the way.
    """
    _wait_for_prompt(bbc)
    program = "".join(f"{10 * (i + 1)} {line}\r" for i, line in enumerate(lines))
    bbc.keyboard.type("NEW\r" + program + "RUN\r")

    def in_bitmap_mode() -> bool:
        # A frame captured mid-reprogramming can carry no bands at all, so the
        # first band is reached only once one exists.
        bands = bbc.video.screen_geometry().bands
        return bool(bands) and bands[0].cell_width != TELETEXT_CELL_WIDTH

    return bbc.run_until_or_timeout(in_bitmap_mode, emulated_seconds=15.0)


class TestScreenTextInTeletext:
    """A MODE 7 display, which the teletext strategy reads exactly."""

    def test_reads_the_boot_banner(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        screen = bbc.video.screen_text()

        assert screen.supported
        assert "BBC Computer" in screen.text
        assert "BASIC" in screen.text

    def test_nothing_it_reads_is_uncertain(self, bbc: Beebium) -> None:
        # Teletext cells are exact character codes, recovered before any pixels
        # exist. There is nothing for a recogniser to be unsure about.
        _wait_for_prompt(bbc)
        screen = bbc.video.screen_text()

        assert screen.unreadable_cells == 0
        assert screen.ambiguous_cells == 0

    def test_returns_runs_and_not_only_a_string(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        screen = bbc.video.screen_text()

        assert len(screen.runs) > 0
        banner = next(run for run in screen.runs if "BBC Computer" in run.text)
        assert banner.cell_width > 0
        assert banner.cell_height > 0
        assert banner.bounds.width > 0

    def test_lines_are_joined_with_lf(self, bbc: Beebium) -> None:
        # The wire carries one canonical form; converting to a platform-native
        # ending is the client's business, where text meets a clipboard.
        _wait_for_prompt(bbc)
        text = bbc.video.screen_text().text

        assert "\r" not in text
        assert "\n" in text

    def test_agrees_with_the_teletext_reading(self, bbc: Beebium) -> None:
        # Both go to the same capture, so they must not disagree about what a
        # MODE 7 screen says.
        _wait_for_prompt(bbc)

        assert bbc.video.screen_text().text == bbc.video.teletext_screen().text

    def test_a_region_reads_only_what_it_covers(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        band = bbc.video.screen_geometry().bands[0]

        one_row = bbc.video.screen_text(
            region=(0, 0, band.column_pitch * 40, band.row_pitch)
        )

        assert one_row.supported
        assert "\n" not in one_row.text

    def test_the_frame_number_advances(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        first = bbc.video.screen_text().frame_number
        bbc.run_until_or_timeout(lambda: False, emulated_seconds=0.5)

        assert bbc.video.screen_text().frame_number > first


class TestTeletextRepertoire:
    """Which meaning a MODE 7 byte carries.

    The SAA5050 draws eleven codes as characters ASCII puts elsewhere, so the
    same screen reads two ways and only the caller knows which was meant.
    """

    # Typed as a BASIC string, read back both ways. All four are codes the
    # SAA5050 draws as something else, and three of them are BASIC syntax.
    DIVERGENT = "[]^~"
    AS_DISPLAYED = "←→↑÷"

    def _print_divergent(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)
        bbc.keyboard.type(f'PRINT "{self.DIVERGENT}"\r')
        assert bbc.run_until_or_timeout(
            lambda: self.DIVERGENT in bbc.video.screen_text().text,
            emulated_seconds=5.0,
        )

    def test_the_default_is_the_codes(self, bbc: Beebium) -> None:
        # A copied BASIC listing keeps its assembler brackets and its
        # exponentiation operator, which is both the commoner capture and the
        # worse one to corrupt.
        self._print_divergent(bbc)

        assert self.DIVERGENT in bbc.video.screen_text().text
        assert self.AS_DISPLAYED not in bbc.video.screen_text().text

    def test_displayed_reports_the_glyphs_the_screen_showed(
        self, bbc: Beebium
    ) -> None:
        self._print_divergent(bbc)

        text = bbc.video.screen_text(characters="displayed").text
        assert self.AS_DISPLAYED in text
        assert self.DIVERGENT not in text

    def test_asking_for_the_codes_explicitly_matches_the_default(
        self, bbc: Beebium
    ) -> None:
        self._print_divergent(bbc)

        assert (
            bbc.video.screen_text(characters="codes").text
            == bbc.video.screen_text().text
        )

    def test_sixels_copy_as_the_blocks_they_drew(self, bbc: Beebium) -> None:
        """A mosaic is a character, and Displayed says which one.

        Unicode's block sextants are the same 2x3 pattern the SAA5050 draws,
        so this is an exact mapping and not a picture of one.
        """
        _wait_for_prompt(bbc)
        # CHR$(151) selects contiguous graphics; then every block, the
        # top-left alone, the left column, and none.
        bbc.keyboard.type(
            'CLS:PRINTCHR$(151);CHR$(255);CHR$(161);CHR$(181);CHR$(160);"Z"\r'
        )
        # Only true once the line has run: CLS wipes the echoed command, whose
        # own "Z" would otherwise satisfy this while it was still being typed.
        assert bbc.run_until_or_timeout(
            lambda: "Z" in bbc.video.screen_text().text
            and "PRINT" not in bbc.video.screen_text().text,
            emulated_seconds=30.0,
        )

        # Under Codes a mosaic byte is a graphics code, not text -- but it
        # still holds its column so the rows stay aligned.
        assert bbc.video.screen_text().text.startswith("     Z")

        shown = bbc.video.screen_text(characters="displayed").text
        # A space for the control code, then the three blocks, then the blank
        # mosaic and the letter. The full and half blocks are the patterns
        # Unicode had before the sextants, so they are not sextants.
        assert shown.startswith(
            " \u2588\U0001FB00\u258c Z"
        ), f"got {shown.splitlines()[0]!r}"

    def test_an_unknown_repertoire_is_refused(self, bbc: Beebium) -> None:
        with pytest.raises(ValueError, match="semaphore"):
            bbc.video.screen_text(characters="semaphore")


class TestScreenTextInBitmapModes:
    """A bitmap display, which the glyph-recognising strategy now reads."""

    def test_mode_4_reads_its_text(self, bbc: Beebium) -> None:
        # The inversion of what this used to assert. A strategy that recognises
        # glyphs in pixels now reads the bitmap display, through the same API
        # and client -- the property the whole design is judged on. Where this
        # once required `not supported` and empty runs, it now requires the
        # opposite: the display is supported and its text comes back.
        #
        # The geometry flips to bitmap the instant MODE 4 runs, which is before
        # the PRINT that follows it has drawn anything, so the text is waited
        # for through the very API under test rather than assumed present.
        assert _switch_to_mode(bbc, 4, "BITMAP TEXT")
        bbc.run_until_or_timeout(
            lambda: "BITMAP TEXT" in bbc.video.screen_text().text,
            emulated_seconds=10.0,
        )

        screen = bbc.video.screen_text()

        assert screen.supported
        assert "BITMAP TEXT" in screen.text
        assert any("BITMAP TEXT" in run.text for run in screen.runs)

    # Every bitmap mode. MODE 2 and MODE 5 are 20 columns wide, so the message
    # is kept short enough to print and echo without wrapping.
    @pytest.mark.parametrize("mode", [0, 1, 2, 3, 4, 5, 6])
    def test_each_bitmap_mode_reads_printed_text(
        self, bbc: Beebium, mode: int
    ) -> None:
        assert _switch_to_mode(bbc, mode, "HELLO BEEB")
        read = bbc.run_until_or_timeout(
            lambda: "HELLO BEEB" in bbc.video.screen_text().text,
            emulated_seconds=10.0,
        )

        assert read
        assert bbc.video.screen_text().supported

    def test_a_bitmap_screen_of_graphics_reads_supported_with_no_text(
        self, bbc: Beebium
    ) -> None:
        # "Graphics, no text" is distinct from "could not read". A cleared
        # graphics screen is read, and simply has nothing on it.
        assert _switch_to_mode(bbc, 4, "X")
        bbc.keyboard.type("CLS\r")
        bbc.run_until_or_timeout(
            lambda: "X" not in bbc.video.screen_text().text,
            emulated_seconds=5.0,
        )

        screen = bbc.video.screen_text()
        assert screen.supported

    def test_graphics_over_text_populates_the_unreadable_count(
        self, bbc: Beebium
    ) -> None:
        # Scattered plotted points are no glyph, so the cells they fall in are
        # reported unreadable rather than guessed at -- the uncertainty the API
        # promises to surface.
        assert _switch_to_mode(bbc, 4, "SPECKLE")
        bbc.keyboard.type("FOR I%=0 TO 600:PLOT 69,I%*2,I%:NEXT\r")
        bbc.run_until_or_timeout(
            lambda: bbc.video.screen_text().unreadable_cells > 0,
            emulated_seconds=10.0,
        )

        assert bbc.video.screen_text().unreadable_cells > 0

    def test_a_bitmap_run_carries_per_cell_readability(
        self, bbc: Beebium
    ) -> None:
        # A run exposes its cells so a client highlights only what was read.
        # Cleanly printed text matches every cell, including its spaces.
        assert _switch_to_mode(bbc, 4, "READABLE")
        bbc.run_until_or_timeout(
            lambda: "READABLE" in bbc.video.screen_text().text,
            emulated_seconds=10.0,
        )

        run = next(
            run for run in bbc.video.screen_text().runs if "READABLE" in run.text
        )
        assert run.cells, "a bitmap run should report its cells"
        assert all(cell.matched for cell in run.cells)

    def test_an_unreadable_cell_is_carried_as_unmatched(
        self, bbc: Beebium
    ) -> None:
        # The count and the per-cell flag agree: when the screen holds ink no
        # glyph fits, some run carries a cell flagged unmatched, so a client can
        # leave it dark rather than highlight a space as if it were read.
        assert _switch_to_mode(bbc, 4, "SPECKLE")
        bbc.keyboard.type("FOR I%=0 TO 600:PLOT 69,I%*2,I%:NEXT\r")
        bbc.run_until_or_timeout(
            lambda: bbc.video.screen_text().unreadable_cells > 0,
            emulated_seconds=10.0,
        )

        screen = bbc.video.screen_text()
        assert screen.unreadable_cells > 0
        assert any(
            not cell.matched for run in screen.runs for cell in run.cells
        )


class TestScreenGeometry:
    """The grid a client snaps a drag to, which every mode has."""

    def test_teletext_reports_one_band_of_forty_by_twenty_five(
        self, bbc: Beebium
    ) -> None:
        _wait_for_prompt(bbc)
        geometry = bbc.video.screen_geometry()

        assert len(geometry.bands) == 1
        band = geometry.bands[0]
        assert (band.bottom - band.top) // band.row_pitch == 25
        assert band.cell_width == 16
        assert band.column_pitch == 16

    def test_a_bitmap_mode_reports_its_own_grid(self, bbc: Beebium) -> None:
        # Reported even though no strategy can read text from the band:
        # snapping is about where the cells are, reading about what is in them.
        assert _switch_to_mode(bbc, 4, "ANYTHING")
        geometry = bbc.video.screen_geometry()

        assert len(geometry.bands) >= 1
        band = geometry.bands[0]
        assert band.cell_width == 8
        assert band.column_pitch == 8
        assert band.cell_height == 8
        assert band.row_pitch == 8

    def test_mode_6_puts_an_eight_line_glyph_on_a_ten_line_pitch(
        self, bbc: Beebium
    ) -> None:
        # The two spare scanlines are blanked rather than painted with the
        # background colour, so the cell is shorter than the step. Reporting
        # the pitch as the cell size is silent while the background is black
        # and total once it is not.
        assert _switch_to_mode(bbc, 6, "ANYTHING")
        geometry = bbc.video.screen_geometry()

        band = geometry.bands[0]
        assert band.row_pitch == 10
        assert band.cell_height == 8

    def test_every_band_has_a_usable_grid(self, bbc: Beebium) -> None:
        _wait_for_prompt(bbc)

        for band in bbc.video.screen_geometry().bands:
            assert band.bottom > band.top
            assert band.cell_width > 0
            assert band.cell_height > 0
            assert band.column_pitch >= band.cell_width
            assert band.row_pitch >= band.cell_height


class TestScreenTextSoftFont:
    """VDU 23 redefinitions, read out of RAM and honoured, or honestly declined."""

    def test_a_redefined_character_reads_once_its_glyph_is_supplied(
        self, bbc: Beebium
    ) -> None:
        # Redefine character 224 -- the range the User Guide teaches -- to a
        # hollow box and print it. The strategy reads the definition from RAM,
        # so the box reads back as character 224 rather than as garbage.
        assert _run_program(
            bbc,
            [
                "MODE 4",
                "VDU 23,224,255,129,129,129,129,129,129,255",
                "PRINT CHR$(224)",
            ],
        )
        # The redefined glyph appears once the PRINT runs, after the mode
        # change the geometry wait keyed on.
        read = bbc.run_until_or_timeout(
            lambda: chr(224) in bbc.video.screen_text().text,
            emulated_seconds=10.0,
        )

        screen = bbc.video.screen_text()
        assert screen.supported
        # Character 224 carries its own code as text: U+00E0 on the wire.
        assert read
        assert chr(224) in screen.text

    def test_a_glyph_drawn_with_a_since_replaced_definition_is_declined(
        self, bbc: Beebium
    ) -> None:
        # A program may redefine a character part way down the screen, so the
        # glyph on screen was drawn with a definition the per-frame font no
        # longer holds -- the one mid-screen redefinition this design does not
        # chase. It must degrade honestly: the stale glyph is declined, never
        # matched to whatever now occupies its code.
        #
        # Here a diamond is printed as character 224, then 224 is redefined to a
        # box. The diamond on screen matches neither the box nor any ROM glyph,
        # so it reads as unread, not as character 224.
        assert _run_program(
            bbc,
            [
                "MODE 4",
                "VDU 23,224,24,60,126,255,255,126,60,24",
                "PRINT CHR$(224)",
                "VDU 23,224,255,129,129,129,129,129,129,255",
                'PRINT "DONE"',
            ],
        )
        # DONE prints after the redefinition, so waiting for it guarantees the
        # font on screen has moved on from the diamond that was drawn.
        assert bbc.run_until_or_timeout(
            lambda: "DONE" in bbc.video.screen_text().text,
            emulated_seconds=10.0,
        )

        screen = bbc.video.screen_text()
        assert screen.supported
        assert chr(224) not in screen.text


class TestScreenTextOffGrid:
    """VDU 5 text, off the character grid: read under Anywhere, not Aligned."""

    def test_off_grid_text_reads_under_anywhere_and_not_aligned(
        self, bbc: Beebium
    ) -> None:
        # VDU 5 sends text to the graphics cursor, at a pixel position that is
        # not on the character grid. The aligned pass walks the grid and misses
        # it; the off-grid pass finds it. The word is drawn by the program, so
        # the echoed source that also contains it is cleared by the mode change
        # before the read.
        assert _run_program(
            bbc,
            [
                "MODE 4",
                "VDU 5",
                "MOVE 100,500",
                'PRINT "OFFGRID"',
                "VDU 4",
            ],
        )
        # The word appears once the off-grid PRINT runs; wait for the search
        # that is meant to find it.
        assert bbc.run_until_or_timeout(
            lambda: "OFFGRID"
            in bbc.video.screen_text(search="anywhere").text,
            emulated_seconds=10.0,
        )

        aligned = bbc.video.screen_text(search="aligned").text
        anywhere = bbc.video.screen_text(search="anywhere").text

        assert "OFFGRID" not in aligned
        assert "OFFGRID" in anywhere

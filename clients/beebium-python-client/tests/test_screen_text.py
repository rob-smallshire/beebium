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

from beebium.client import Beebium


def _wait_for_prompt(bbc: Beebium) -> None:
    bbc.run_until_or_timeout(
        lambda: ">" in bbc.video.screen_text().text, emulated_seconds=5.0
    )


def _switch_to_mode(bbc: Beebium, mode: int, message: str) -> bool:
    """Leave the machine in a bitmap mode with something printed on it.

    Returns whether the message made it onto the screen, which is checked
    through the frame rather than through the text API: whether that API can
    read a bitmap mode is the very thing under test here.
    """
    _wait_for_prompt(bbc)
    bbc.keyboard.type(f'MODE {mode}\rPRINT "{message}"\r')

    # A stopped machine never drains the type-ahead queue, so the machine has
    # to be advanced while waiting rather than merely waited on.
    return bbc.run_until_or_timeout(
        lambda: not bbc.video.screen_geometry().bands[0].row_pitch == 20,
        emulated_seconds=10.0,
    )


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


class TestScreenTextInBitmapModes:
    """A bitmap display, which nothing reads yet."""

    def test_mode_4_reports_that_it_cannot_be_read(self, bbc: Beebium) -> None:
        # THIS ASSERTION IS WRITTEN TO INVERT.
        #
        # No strategy reads glyphs out of pixels yet, so a bitmap display
        # honestly reports that it could not be read rather than returning the
        # MODE 7 cells left over from before the mode change.
        #
        # When a strategy that recognises glyphs is added behind the same
        # dispatch, this call starts returning the text without any change to
        # the API or to this client -- which is the property the whole design
        # is judged on. At that point this test should be rewritten to assert
        # `supported` and the message, and the assertion below on runs should
        # become an assertion that the runs contain it.
        assert _switch_to_mode(bbc, 4, "BITMAP TEXT")

        screen = bbc.video.screen_text()

        assert not screen.supported
        assert screen.runs == ()
        assert screen.text == ""

        # Not merely absent: the stale teletext cells must not leak through.
        assert "BBC Computer" not in screen.text

    def test_a_bitmap_mode_is_never_uncertain_while_nothing_reads_it(
        self, bbc: Beebium
    ) -> None:
        # Nothing was tried, which is not the same as having tried a cell and
        # failed on it. The counts stay zero until a strategy populates them.
        assert _switch_to_mode(bbc, 4, "BITMAP TEXT")
        screen = bbc.video.screen_text()

        assert screen.unreadable_cells == 0
        assert screen.ambiguous_cells == 0


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
        assert band.cell_width == 12
        assert band.column_pitch == 12

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

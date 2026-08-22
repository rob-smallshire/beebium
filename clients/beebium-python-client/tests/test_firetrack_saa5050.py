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

"""Firetrack's custom SAA5050 screen modes.

Firetrack draws its intro credits in a teletext (SAA5050) mode that is not the
standard MODE 7 shape: the CRTC is programmed for 50 displayed character columns
and 18 rows, rather than 40 by 25. It is a good, real-world exercise of a custom
SAA5050 mode -- something no synthetic test covers -- and it exposes places where
the emulator's screen services still assume the fixed 40x25 grid.

The boot/navigation procedure is shared (see firetrack.py) so other Firetrack
tests can reuse it.
"""

from __future__ import annotations

import os

import pytest

from beebium.client import Beebium

import firetrack

# Firetrack drives a real game through its multi-minute instruction story in real
# time before the custom SAA5050 intro appears. That end-to-end navigation is
# slow and timing-fragile on shared CI runners -- a fast host can race past the
# transient MODE 4 loader between screen samples -- while the teletext behaviour
# it exercises is already covered by the C++ unit tests (test_teletext_grid,
# test_screen_text). So it is skipped on CI, and kept for local and manual runs;
# set BEEBIUM_RUN_SLOW_TESTS=1 to run it on CI anyway (e.g. a nightly job).
pytestmark = pytest.mark.skipif(
    bool(os.environ.get("CI")) and not os.environ.get("BEEBIUM_RUN_SLOW_TESTS"),
    reason="Slow, timing-fragile game navigation; run locally or set "
    "BEEBIUM_RUN_SLOW_TESTS=1",
)

# The custom mode Firetrack programs for its intro screens (CRTC registers).
CUSTOM_COLUMNS = 50   # R1, horizontal displayed (standard MODE 7 is 40)
CUSTOM_ROWS = 18      # R6, vertical displayed   (standard MODE 7 is 25)
TELETEXT_CELL_WIDTH = 16  # the SAA5050 cell, distinguishing teletext from bitmap


def _at_intro(bbc: Beebium) -> None:
    firetrack.advance_to_saa5050_intro(bbc)
    bbc.debugger.ensure_stopped()  # a stable snapshot to read state from


class TestFiretrackCustomSaa5050:
    def test_intro_is_a_custom_shaped_teletext_mode(self, bbc_firetrack: Beebium) -> None:
        """The intro is a teletext band on a CRTC programmed 50x18, not 40x25."""
        bbc = bbc_firetrack
        _at_intro(bbc)

        r = bbc.crtc.state.registers
        assert r[1] == CUSTOM_COLUMNS, f"R1 (columns) was {r[1]}"
        assert r[6] == CUSTOM_ROWS, f"R6 (rows) was {r[6]}"
        # Non-standard on both axes: wider and shorter than MODE 7.
        assert r[1] > 40 and r[6] < 25

        geometry = bbc.video.screen_geometry()
        assert len(geometry.bands) == 1
        # A teletext band (SAA5050 16-pixel cell), not a bitmap one.
        assert geometry.bands[0].cell_width == TELETEXT_CELL_WIDTH

    def test_intro_credits_are_readable(self, bbc_firetrack: Beebium) -> None:
        """The intro text is recoverable from the custom SAA5050 screen."""
        bbc = bbc_firetrack
        _at_intro(bbc)
        text = bbc.video.screen_text().text
        assert "Aardvark" in text
        assert "Software Studios" in text

    def test_teletext_screen_reports_the_full_custom_width(
        self, bbc_firetrack: Beebium
    ) -> None:
        """The attribute API captures all 50 columns, not a 40-column corner.

        The teletext grid grows to the displayed shape, so ``teletext_screen()``
        reports 50 columns by 18 rows and carries the cells past column 40 that
        a fixed 40x25 grid would have dropped.
        """
        bbc = bbc_firetrack
        _at_intro(bbc)

        screen = bbc.video.teletext_screen()
        assert screen.active
        assert screen.columns == CUSTOM_COLUMNS
        assert screen.rows == CUSTOM_ROWS
        assert len(screen.cells) == CUSTOM_ROWS * CUSTOM_COLUMNS

        # Something is drawn past the standard 40-column edge -- proof the wide
        # cells are captured rather than truncated.
        STANDARD_COLUMNS = 40
        beyond_the_standard_edge = any(
            screen.cell(row, column).character not in (0, ord(" "))
            for row in range(screen.rows)
            for column in range(STANDARD_COLUMNS, screen.columns)
        )
        assert beyond_the_standard_edge

    def test_geometry_reports_the_custom_row_count(self, bbc_firetrack: Beebium) -> None:
        """The band derives the true 18 rows, not the standard-MODE-7 25.

        The teletext row pitch comes from the SAA5050's fixed character height
        (ten font rows, doubled to twenty by interlace), so the 360-scanline
        custom mode reports 18 rows rather than 500/25's assumed 25.
        """
        bbc = bbc_firetrack
        _at_intro(bbc)
        band = bbc.video.screen_geometry().bands[0]
        derived_rows = (band.bottom - band.top) // band.row_pitch
        assert derived_rows == CUSTOM_ROWS

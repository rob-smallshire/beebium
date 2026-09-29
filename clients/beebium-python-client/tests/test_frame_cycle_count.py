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

"""Frame.cycle_count is the emulated cycle at which the frame completed (#111)."""

from __future__ import annotations

from beebium.client import Beebium

# 0.1 emulated seconds: several fields, so at least one frame completes.
RUN_SECONDS = 0.1
RUN_CYCLES = 200_000


def test_captured_frame_completed_during_the_run(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    before = bbc.debugger.cycle_count
    bbc.run_for_emulated_seconds(RUN_SECONDS)
    after = bbc.debugger.cycle_count

    frame = bbc.video.capture_frame()

    assert before < frame.cycle_count <= after


def test_successive_captures_advance(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    bbc.run_for_emulated_seconds(RUN_SECONDS)
    first = bbc.video.capture_frame()
    previous_cycle_count = bbc.debugger.cycle_count

    bbc.run_for_emulated_seconds(RUN_SECONDS)
    second = bbc.video.capture_frame()

    assert second.cycle_count >= previous_cycle_count
    assert second.cycle_count - first.cycle_count <= 2 * RUN_CYCLES

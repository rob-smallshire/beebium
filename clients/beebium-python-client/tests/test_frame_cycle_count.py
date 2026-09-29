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

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import TimeoutError

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


def test_capture_after_a_cycle_returns_a_frame_completed_after_it(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    bbc.run_for_emulated_seconds(RUN_SECONDS)
    reached = bbc.debugger.cycle_count
    bbc.debugger.ensure_running()
    try:
        frame = bbc.video.capture_frame(timeout=5.0, after_cycle=reached)
    finally:
        bbc.debugger.ensure_stopped()
    assert frame.cycle_count >= reached
    # The first frame to complete after it: within two interlaced frames.
    assert frame.cycle_count < reached + 2 * 80_000


def test_capture_after_a_cycle_times_out_on_a_stopped_machine(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    with pytest.raises(TimeoutError, match="No frame completed at or after cycle"):
        bbc.video.capture_frame(timeout=0.3, after_cycle=bbc.debugger.cycle_count + 1_000_000)


def test_crtc_reports_the_beam_within_the_field(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    bbc.run_for_emulated_seconds(RUN_SECONDS)
    first = bbc.crtc.state
    bbc.debugger.step_cycles(10)
    second = bbc.crtc.state
    # Ten cycles on, in the same field unless a vsync fell between them.
    if second.cycles_since_vsync >= first.cycles_since_vsync:
        assert second.cycles_since_vsync - first.cycles_since_vsync == 10
        assert second.odd_field == first.odd_field
    assert first.cycles_since_vsync < 41_000

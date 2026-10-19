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

"""Running by fields, and frames stamped with the field that completed them (#190).

The server counts vsync rising edges as ``field``; ``run_fields(n)`` stops
at the first instruction boundary at or after the nth edge, and the frame the
edge completed carries the same count, so a test can run a field, look at the
frame, and know which field it is looking at. The display tests use that to
pin when a change takes effect: an R12/R13 write at the next frame, a B+
shadow switch at the next character fetch.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium
from beebium.client.video import Frame

MODE4_SCREEN = 0x5800
MODE4_SCREEN_BYTES = 0x2800
OTHER_SCREEN = 0x3000          # another 10K, below MODE 4's HIMEM
SPLIT_ROW = 12                 # the character row the beam is on at the switch
SCANLINES_PER_ROW = 8


def _mode4(bbc: Beebium) -> None:
    """MODE 4 with the cursor off, so nothing but screen memory is drawn."""
    bbc.system.set_speed_multiplier(0.0)
    bbc.wait_until_ready()
    bbc.keyboard.type("MODE 4:VDU 23,1,0;0;0;0;\r")
    bbc.keyboard.wait_until_typing_complete()
    bbc.run_fields(10)  # the MODE change and the prompt are done


def _white_fraction(frame: Frame, y: int) -> float:
    row = frame.pixels[y * frame.width * 4:(y + 1) * frame.width * 4]
    white = sum(1 for x in range(0, len(row), 4) if row[x] > 0x80)
    return white / frame.width


def _rows(frame: Frame) -> list[float]:
    return [_white_fraction(frame, y) for y in range(frame.height)]


def _stop_on_row(bbc: Beebium, row: int) -> None:
    """Stop with the beam on `row` of the display."""
    with bbc.debugger.breakpoint(0x0000, end_address=0x10000,
                                 condition=f"crtc_row == {row} && display_enabled"):
        bbc.debugger.run_and_wait_for_stop(timeout=5.0)


class TestRunFields:
    def test_stops_on_the_nth_field_and_leaves_the_machine_stopped(self, launch_bbc) -> None:
        bbc = launch_bbc()
        bbc.system.set_speed_multiplier(0.0)
        bbc.debugger.ensure_stopped()
        start = bbc.crtc.state.field
        state = bbc.run_fields(3)
        assert bbc.crtc.state.field == start + 3
        assert not state.is_running
        assert bbc.debugger.is_stopped
        assert state.at_instruction_boundary
        # Just past the edge: within one instruction of it.
        assert bbc.crtc.state.cycles_since_vsync < 8

    def test_refuses_fewer_than_one_field(self, launch_bbc) -> None:
        with pytest.raises(ValueError):
            launch_bbc().run_fields(0)

    def test_a_stopped_capture_is_the_frame_of_the_field_just_run(self, launch_bbc) -> None:
        """In a progressive mode every field edge completes a frame."""
        bbc = launch_bbc()
        _mode4(bbc)
        for _ in range(5):
            bbc.run_fields(1)
            frame = bbc.video.capture_frame(timeout=2.0)
            assert frame.field == bbc.crtc.state.field
            assert frame.cycle_count == bbc.debugger.cycle_count - bbc.crtc.state.cycles_since_vsync

    def test_in_mode_7_every_other_edge_completes_an_interlaced_frame(self, launch_bbc) -> None:
        bbc = launch_bbc()
        bbc.system.set_speed_multiplier(0.0)
        bbc.wait_until_ready()
        completed = []
        for _ in range(6):
            bbc.run_fields(1)
            frame = bbc.video.capture_frame(timeout=2.0)
            assert bbc.crtc.state.field - frame.field in (0, 1)
            completed.append(frame.field)
        # Frames complete on edges of one parity only, two fields apart.
        assert len({field % 2 for field in completed}) == 1
        assert sorted(set(completed)) == list(range(min(completed), max(completed) + 1, 2))


class TestWhenDisplayChangesTakeEffect:
    def test_an_r12_r13_write_takes_effect_at_the_next_frame(self, launch_bbc) -> None:
        bbc = launch_bbc()
        _mode4(bbc)
        bbc.memory.address.bus.write(MODE4_SCREEN, b"\x00" * MODE4_SCREEN_BYTES)
        bbc.memory.address.bus.write(OTHER_SCREEN, b"\xff" * MODE4_SCREEN_BYTES)

        _stop_on_row(bbc, SPLIT_ROW)
        drawing = bbc.crtc.state.field  # the field the beam is drawing
        start = OTHER_SCREEN // 8
        for register, value in ((12, start >> 8), (13, start & 0xFF)):
            bbc.memory.address.bus.write(0xFE00, bytes([register]))
            bbc.memory.address.bus.write(0xFE01, bytes([value]))

        bbc.run_fields(1)
        frame = bbc.video.capture_frame(timeout=2.0)
        assert frame.field == drawing + 1
        assert max(_rows(frame)) == 0.0, "the frame being drawn kept its start address"

        bbc.run_fields(1)
        frame = bbc.video.capture_frame(timeout=2.0)
        assert frame.field == drawing + 2
        assert min(_rows(frame)) == 1.0, "the next frame starts at the new address"

    def test_a_b_plus_shadow_switch_takes_effect_at_the_next_character_fetch(self, launch_bbc) -> None:
        bbc = launch_bbc(variant="model-b-plus")
        _mode4(bbc)
        bbc.memory.address.bus.write(MODE4_SCREEN, b"\x00" * MODE4_SCREEN_BYTES)
        bbc.memory.region("shadow_ram").bus.write(MODE4_SCREEN, b"\xff" * MODE4_SCREEN_BYTES)

        _stop_on_row(bbc, SPLIT_ROW)
        drawing = bbc.crtc.state.field
        raster = bbc.crtc.state.raster
        bbc.memory.address.bus.write(0xFE34, bytes([0x80]))  # ACCCON: display shadow RAM

        bbc.run_fields(1)
        frame = bbc.video.capture_frame(timeout=2.0)
        assert frame.field == drawing + 1
        rows = _rows(frame)
        beam_line = SPLIT_ROW * SCANLINES_PER_ROW + raster
        # Main RAM above the beam, shadow RAM from the next scanline on: the
        # 6845 fetches through the switch, so the split is where the beam was.
        assert max(rows[:beam_line]) == 0.0, rows[:beam_line]
        assert min(rows[beam_line + 1:]) == 1.0, rows[beam_line + 1:]

        bbc.run_fields(1)
        frame = bbc.video.capture_frame(timeout=2.0)
        assert min(_rows(frame)) == 1.0, "the whole next frame is shadow RAM"

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

"""What launch() leaves the machine doing (#198).

launch() leaves the machine running, started by the client once it has
connected, so a launch means the same thing in emulated time on every host.
With start_stopped=True the machine has executed no instruction: the reset
sequence has run and it waits at the reset vector, so breakpoints set before
run() see everything the guest does.
"""

from __future__ import annotations

import time

from beebium.client import Beebium

RESET_SEQUENCE_CYCLES = 7


def _reset_vector(bbc: Beebium) -> int:
    low, high = bbc.memory.address.peek.read(0xFFFC, 2)
    return low | high << 8


def test_launch_leaves_the_machine_running(launch_bbc) -> None:
    bbc = launch_bbc()
    assert bbc.debugger.is_running
    # is_running is the server's word; the cycle count shows the machine
    # really moves. Wait for it to move, since a loaded host may not schedule
    # the emulation thread within any fixed interval.
    start = bbc.debugger.cycle_count
    deadline = time.monotonic() + 10.0
    cycle_count = start
    while cycle_count <= start and time.monotonic() < deadline:
        time.sleep(0.01)
        cycle_count = bbc.debugger.cycle_count
    assert cycle_count > start


def test_start_stopped_leaves_the_machine_at_the_reset_vector(launch_bbc) -> None:
    bbc = launch_bbc(start_stopped=True)
    assert bbc.debugger.is_stopped
    assert bbc.debugger.cycle_count == RESET_SEQUENCE_CYCLES
    assert bbc.cpu.pc == _reset_vector(bbc)
    time.sleep(0.2)
    assert bbc.debugger.cycle_count == RESET_SEQUENCE_CYCLES


def test_a_breakpoint_set_before_run_sees_the_first_instructions(launch_bbc) -> None:
    bbc = launch_bbc(start_stopped=True)
    second_instruction = _reset_vector(bbc) + 2  # MOS 1.20 opens with LDA #&40
    with bbc.debugger.breakpoint(second_instruction):
        bbc.debugger.run_and_wait_for_stop(timeout=5.0)
    assert bbc.cpu.pc == second_instruction
    assert bbc.debugger.cycle_count == RESET_SEQUENCE_CYCLES + 2

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

"""Driving and debugging a coprocessor, and TubeSystem (#204).

The coprocessor runs on the host's emulation thread and advances only while
the host runs. These tests load a small program into parasite memory and put
the parasite's PC on it, then drive it through TubeSystem and the coprocessor's
own debugger.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import DebuggerError
from beebium.client.tube_system import TubeSystem

PROGRAM_ADDRESS = 0x2000
LOOP_DONE = 0x2007
COUNTER = 0x70
PROGRAM = bytes([
    0xA2, 0x00,        # 2000 LDX #0
    0xE8,              # 2002 INX
    0xE0, 0x0A,        # 2003 CPX #10
    0xD0, 0xFB,        # 2005 BNE &2002
    0xE6, COUNTER,     # 2007 INC &70
    0x4C, 0x00, 0x20,  # 2009 JMP &2000
])
# LDX 2, ten INX + CPX at 4 each, nine taken BNEs at 3 and one not taken at 2.
CYCLES_TO_LOOP_DONE = 2 + 10 * 4 + 9 * 3 + 2


@pytest.fixture
def system(launch_bbc, tmp_path: Path) -> TubeSystem:
    bbc = launch_bbc(preset="model-b-disc-65c02-copro", startup_timeout=30.0)
    bbc.system.set_speed_multiplier(0.0)
    bbc.wait_until_ready()
    system = TubeSystem.from_host(bbc)
    system.stop()

    # A block load into the parasite's memory, from a file.
    program_filepath = tmp_path / "program.bin"
    program_filepath.write_bytes(PROGRAM)
    coprocessor = system.coprocessor
    assert coprocessor.memory.address.bus.load(PROGRAM_ADDRESS, program_filepath) == len(PROGRAM)
    coprocessor.memory.address.bus[COUNTER] = 0
    coprocessor.cpu.pc = PROGRAM_ADDRESS
    return system


def _counter(system: TubeSystem) -> int:
    return system.coprocessor.memory.address.peek[COUNTER]


def test_the_block_load_lands_in_parasite_memory_not_the_hosts(system: TubeSystem) -> None:
    assert system.coprocessor.memory.address.peek.read(PROGRAM_ADDRESS, len(PROGRAM)) == PROGRAM
    assert system.host.memory.address.peek.read(PROGRAM_ADDRESS, len(PROGRAM)) != PROGRAM


class TestTubeSystem:
    def test_run_runs_both_and_stop_stops_both(self, system: TubeSystem) -> None:
        system.run()
        assert system.host.debugger.is_running
        assert system.coprocessor.debugger.is_running
        system.stop()
        assert system.host.debugger.is_stopped
        assert system.coprocessor.debugger.is_stopped

    def test_run_for_advances_both_and_leaves_both_stopped(self, system: TubeSystem) -> None:
        host_start = system.host.debugger.cycle_count
        system.run_for(0.1)
        assert system.host.debugger.cycle_count >= host_start + 200_000
        assert _counter(system) > 0
        assert system.host.debugger.is_stopped
        assert system.coprocessor.debugger.is_stopped

    def test_run_until_or_timeout_stops_both_once_the_predicate_holds(self, system: TubeSystem) -> None:
        assert system.run_until_or_timeout(lambda: _counter(system) >= 5, 1.0, chunk_seconds=0.01)
        assert _counter(system) >= 5
        assert system.host.debugger.is_stopped
        assert system.coprocessor.debugger.is_stopped

    def test_run_until_or_timeout_reports_a_predicate_never_met(self, system: TubeSystem) -> None:
        assert not system.run_until_or_timeout(lambda: False, 0.05, chunk_seconds=0.01)
        assert system.coprocessor.debugger.is_stopped


class TestTheCoprocessorDebugger:
    def test_a_run_with_the_host_stopped_is_refused(self, system: TubeSystem) -> None:
        with pytest.raises(DebuggerError, match=r"the host is stopped.*TubeSystem\.run\(\)"):
            system.coprocessor.debugger.run()
        assert system.coprocessor.debugger.is_stopped

    def test_a_run_to_with_the_host_stopped_is_refused(self, system: TubeSystem) -> None:
        with pytest.raises(DebuggerError, match="the host is stopped"):
            system.coprocessor.debugger.run_to(LOOP_DONE)

    def test_stepping_needs_no_running_host(self, system: TubeSystem) -> None:
        system.coprocessor.debugger.step()
        assert system.coprocessor.cpu.pc == PROGRAM_ADDRESS + 2
        assert system.coprocessor.cpu.x == 0
        assert system.host.debugger.is_stopped

    def test_a_breakpoint_stops_the_coprocessor_and_not_the_host(self, system: TubeSystem) -> None:
        system.host.debugger.run()
        with system.coprocessor.debugger.breakpoint(LOOP_DONE):
            system.coprocessor.debugger.run_and_wait_for_stop(timeout=5.0)
        assert system.coprocessor.cpu.pc == LOOP_DONE
        assert system.coprocessor.cpu.x == 10
        assert system.host.debugger.is_running

    def test_run_to(self, system: TubeSystem) -> None:
        system.host.debugger.run()
        system.coprocessor.debugger.run_to(LOOP_DONE)
        assert system.coprocessor.cpu.pc == LOOP_DONE
        assert system.coprocessor.cpu.x == 10
        assert _counter(system) == 0

    def test_measure_counts_the_coprocessors_own_cycles(self, system: TubeSystem) -> None:
        system.host.debugger.run()
        measurement = system.coprocessor.debugger.measure(PROGRAM_ADDRESS, LOOP_DONE, repeat=3)
        assert measurement.samples == (CYCLES_TO_LOOP_DONE,) * 3


def test_a_b_plus_128k_boots_basic_onto_the_parasite(launch_bbc) -> None:
    """The B+ 128K with a 65C02 boots with the Tube in and BASIC on the parasite."""
    bbc = launch_bbc(preset="model-b-plus-128k-65c02-copro", variant="model-b-plus-128k",
                     startup_timeout=30.0)
    bbc.system.set_speed_multiplier(0.0)

    def screen() -> str:
        return bbc.video.screen_text().text

    assert bbc.run_until_or_timeout(lambda: "Acorn TUBE 6502 64K" in screen(), 5.0), screen()
    bbc.debugger.run()
    bbc.keyboard.type("PRINT ~PAGE\r")
    coprocessor = bbc.connect_coprocessor()
    coprocessor.debugger.run_to(0xFFEE)  # the parasite's OSWRCH
    assert coprocessor.cpu.pc == 0xFFEE
    coprocessor.debugger.ensure_running()
    bbc.keyboard.wait_until_typing_complete()
    assert bbc.run_until_or_timeout(
        lambda: [line.strip() for line in screen().splitlines() if line.strip()][-2:] == ["800", ">"],
        2.0,
    ), screen()

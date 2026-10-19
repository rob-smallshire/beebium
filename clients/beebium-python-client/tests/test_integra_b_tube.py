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

"""An Integra-B launched with a coprocessor boots with the Tube in (#203).

IBOS takes the Tube up at a hard reset only when configured TUBE (user register
&0F bit 0 clear). A board launched with a coprocessor attached is seeded that
way, so the parasite's BASIC is running from the first prompt.
"""

from __future__ import annotations

from beebium.client import Beebium

TUBE_USER_REGISTER = 0x0F
NOTUBE_BIT = 0x01
PARASITE_OSWRCH = 0xFFEE


def _screen(bbc: Beebium) -> str:
    return bbc.video.screen_text().text


def _result_line(bbc: Beebium) -> str:
    lines = [line.strip() for line in _screen(bbc).splitlines() if line.strip()]
    return lines[-2] if len(lines) >= 2 and lines[-1] == ">" else ""


def _launch(launch_bbc, **kwargs) -> Beebium:
    bbc = launch_bbc(variant="model-b-integra-b", **kwargs)
    bbc.system.set_speed_multiplier(0.0)
    return bbc


def test_boots_with_the_tube_in_and_runs_basic_on_the_parasite(launch_bbc) -> None:
    bbc = _launch(launch_bbc, preset="model-b-integra-b-disc-65c02-copro")
    assert bbc.run_until_or_timeout(lambda: "Acorn TUBE 6502 64K" in _screen(bbc), 5.0), _screen(bbc)
    assert not bbc.real_time_clock.state.cmos_ram[TUBE_USER_REGISTER] & NOTUBE_BIT

    # BASIC runs on the parasite: PAGE is &800 (on the host it would be &1900
    # with DFS), and printing goes through the parasite's OSWRCH.
    bbc.debugger.run()
    bbc.keyboard.type("PRINT ~PAGE\r")
    coprocessor = bbc.connect_coprocessor()
    state = coprocessor.debugger.run_to(PARASITE_OSWRCH)
    assert not state.is_running
    assert coprocessor.cpu.pc == PARASITE_OSWRCH
    bbc.debugger.ensure_running()
    coprocessor.debugger.ensure_running()
    bbc.keyboard.wait_until_typing_complete()
    assert bbc.run_until_or_timeout(lambda: _result_line(bbc) == "800", 2.0), _screen(bbc)


def test_without_a_coprocessor_the_board_is_notube(launch_bbc) -> None:
    bbc = _launch(launch_bbc, preset="model-b-integra-b-disc")
    assert bbc.run_until_or_timeout(lambda: ">" in _screen(bbc), 5.0), _screen(bbc)
    assert "TUBE" not in _screen(bbc)
    assert bbc.real_time_clock.state.cmos_ram[TUBE_USER_REGISTER] & NOTUBE_BIT


def test_a_machine_without_a_clock_of_its_own_reports_none(launch_bbc) -> None:
    state = launch_bbc().real_time_clock.state
    assert not state.present
    assert state.registers == b""

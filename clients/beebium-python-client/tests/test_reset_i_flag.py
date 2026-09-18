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

"""Scenario guard for issue #78: Ctrl-Break must recover a machine that has an
interrupt pending after running off into an empty sideways bank.

Poisoning the ROM type table (``?&2A1=&FF`` marks bank 0 as holding a service
ROM) and then issuing ``*HELP`` sends the MOS into the empty bank, whose all-&FF
contents execute as garbage with interrupts masked; a System VIA interrupt goes
pending and stays pending. Ctrl-Break is the documented recovery.

Before the #78 fix the 6502 core's reset sequence left the I flag clear, so with
an interrupt pending the CPU left reset through the IRQ vector (&FFFE) instead of
the reset vector (&FFFC): the MOS reset code never ran, the banner never
returned, &028D stayed at its power-on value and the poisoned ROM type table was
never rebuilt. With the fix, RESET sets I, the reset handler runs, and Ctrl-Break
recovers the machine: the banner returns, &028D records a hard reset (2) and the
bank-0 ROM type entry is rebuilt to 0 (empty).

Note: a plain (soft) Break is deliberately NOT asserted to recover here -- MOS
1.20 preserves the ROM type table across a soft Break, which is faithful
non-recovery. Ctrl-Break is the recovery, as on hardware.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import read_mode7_screen, screen_contains

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Break/reset timing is sensitive on Windows CI runners",
)

ROM_TYPE_TABLE = 0x02A1  # bank N type at ROM_TYPE_TABLE + N
LAST_BREAK_TYPE = 0x028D  # 0 = soft, 1 = power-on, 2 = Ctrl (hard)

# This scenario is driven in real (wall-clock) time rather than by an emulated
# cycle budget, and deliberately so: the whole point of the test is a guest that
# runs off into an empty bank and executes garbage. That garbage reaches an
# undefined "jam" (KIL) opcode, which halts the CPU and freezes the cycle
# counter, so a cycle-budget wait (run_until_or_timeout / step_cycles) never
# reaches its target and hangs. Only a hardware reset (Break) revives the
# machine. Beebium paces to real BBC speed, so a wall-clock sleep advances a
# comparable span of emulated time; the waits below are bounded.


def _run_free(bbc: Beebium, seconds: float) -> None:
    """Let the machine run at real speed for a bounded wall-clock span."""
    bbc.debugger.ensure_running()
    time.sleep(seconds)


def _wait_for(bbc: Beebium, predicate, timeout_seconds: float) -> bool:
    """Poll predicate() at real-BBC pace until true or the budget expires."""
    bbc.debugger.ensure_running()
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.1)
    return predicate()


def _read_byte(bbc: Beebium, address: int) -> int:
    # peek is side-effect-free and quiesces the emulation thread, so it is safe
    # while the machine is running.
    return bbc.memory.address.peek[address]


def _erase_screen(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    bbc.memory.address.bus[0x7C00:0x8000] = bytes([0x20] * 0x400)
    bbc.debugger.ensure_running()


@pytest.fixture
def bbc_model_b(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    tmp_path: pytest.TempPathFactory,
    monkeypatch: pytest.MonkeyPatch,
) -> Beebium:
    """A stock Model B + 1770 DFS booted to BASIC, isolated per test."""
    monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(Path(tmp_path) / "disc_work"))
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            booted = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "BASIC"),
                emulated_seconds=15.0,
            )
            if not booted:
                rows = read_mode7_screen(bbc)
                for i, row in enumerate(rows):
                    print(f"Row {i:2d}: [{row}]")
                pytest.fail("BASIC banner did not appear after boot")
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_skip_windows_ci
class TestResetIFlag:
    """Issue #78: Ctrl-Break recovers a machine with a pending interrupt."""

    def test_ctrl_break_recovers_after_wandering_into_empty_bank(
        self, bbc_model_b: Beebium
    ) -> None:
        bbc = bbc_model_b

        # Poison the ROM type table so the MOS believes bank 0 holds a service
        # ROM, then *HELP sends it into the empty bank's garbage.
        bbc.debugger.ensure_running()
        bbc.keyboard.type("?&2A1=&FF\r")
        bbc.keyboard.type("*HELP\r")

        # Let the machine wander into the empty bank (interrupts get masked and a
        # System VIA interrupt goes pending; the CPU eventually jams).
        _run_free(bbc, 3.0)

        # Erase the screen so the banner's return is a real signal.
        _erase_screen(bbc)

        # Ctrl-Break: hold Ctrl across the Break, and run while it is still held
        # so the MOS reset routine reads the matrix and treats it as a hard
        # reset.
        bbc.keyboard.ctrl_down()
        assert bbc.keyboard.break_down(), "BreakDown RPC failed"
        _run_free(bbc, 0.1)
        assert bbc.keyboard.break_up(), "BreakUp RPC failed"
        _run_free(bbc, 1.0)
        bbc.keyboard.ctrl_up()

        # Give the MOS reset code time to redraw and re-initialise.
        recovered = _wait_for(
            bbc, lambda: screen_contains(bbc, "BASIC"), timeout_seconds=5.0
        )
        if not recovered:
            rows = read_mode7_screen(bbc)
            print("\nScreen after Ctrl-Break (issue #78 -- no recovery):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
        assert recovered, "Banner did not return after Ctrl-Break (issue #78)."

        last_break = _read_byte(bbc, LAST_BREAK_TYPE)
        bank0_type = _read_byte(bbc, ROM_TYPE_TABLE)
        assert last_break == 2, (
            f"&028D = {last_break}, expected 2 (Ctrl/hard Break); the MOS reset "
            "code did not run (issue #78)."
        )
        assert bank0_type == 0, (
            f"&02A1 (bank 0 ROM type) = {bank0_type:#04x}, expected 0; the ROM "
            "type table was not rebuilt (issue #78)."
        )

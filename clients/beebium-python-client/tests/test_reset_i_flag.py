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


def _read_word(bbc: Beebium, address: int) -> int:
    lo = bbc.memory.address.peek[address]
    hi = bbc.memory.address.peek[address + 1]
    return lo | (hi << 8)


def _count_on_screen(bbc: Beebium, text: str) -> int:
    return sum(row.count(text) for row in read_mode7_screen(bbc))


def _step_run(bbc: Beebium, emulated_seconds: float) -> None:
    """Advance a fixed span of emulated time by DEBUGGER stepping.

    Unlike free-run (run_until_or_timeout), debugger stepping keeps advancing
    the emulated clock and ticking the peripherals even while the reset line is
    held, so the free-running System VIA timer raises an interrupt during a
    Break hold exactly as it does on real hardware. All emulated time, no
    wall-clock sleep.
    """
    hz = bbc.system.clock_speed_hz or 2_000_000
    bbc.debugger.ensure_stopped()
    bbc.debugger.step_cycles(max(1, int(emulated_seconds * hz)))


def _step_until(bbc: Beebium, predicate, emulated_seconds: float,
                chunk_seconds: float = 0.1) -> bool:
    """Debugger-step in fixed chunks until predicate() or the budget expires.

    Stepping advances even when the CPU is halted (Break held, or a jam), so
    this cannot hang the way a cycle-budget free-run wait does."""
    hz = bbc.system.clock_speed_hz or 2_000_000
    remaining = int(emulated_seconds * hz)
    chunk = max(1, int(chunk_seconds * hz))
    bbc.debugger.ensure_stopped()
    if predicate():
        return True
    while remaining > 0:
        step = min(chunk, remaining)
        bbc.debugger.step_cycles(step)
        remaining -= step
        if predicate():
            return True
    return False


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

    def test_break_recovers_with_an_irq_raised_while_break_is_held(
        self, bbc_model_b: Beebium
    ) -> None:
        """Type-in regression for the core defect (basis of an upstream report).

        A BASIC program masks off every System VIA interrupt but the keyboard
        (CA2), points IRQ1V at a planted JAM opcode, and spins with interrupts
        enabled. Holding Break, tapping the space bar, then releasing Break
        raises a CA2 interrupt while the CPU is halted, so an IRQ is pending at
        the instant reset completes. Before the fix the CPU left reset through
        the IRQ vector, ran IRQ1V into the JAM and halted: PC static at &0900,
        blank screen, &028D stuck at its power-on value. With the fix RESET sets
        I, the pending IRQ stays masked, the MOS reset code runs, the banner
        returns, &028D records a soft Break (0) and the MOS rebuilds IRQ1V.

        Timing is paced in emulated time: the machine runs a clean BASIC loop
        until Break (so the cycle counter advances and emulated-time waits are
        reliable here), and the program's own output is polled for.
        """
        bbc = bbc_model_b

        program = (
            "10 REM BREAK WITH AN IRQ PENDING\r"
            "20 T%=TIME:REPEAT UNTIL TIME>T%+100\r"
            "30 ?&900=2:REM JAM OPCODE\r"
            "40 ?&FE4E=&7E:REM ONLY THE KEYBOARD IRQ LEFT ENABLED\r"
            "50 ?&204=0:?&205=9:REM IRQ1V -> &0900\r"
            '60 PRINT "HOLD BREAK, TAP SPACE, RELEASE BREAK"\r'
            "70 REPEAT UNTIL FALSE\r"
        )
        bbc.keyboard.type(program)

        # Let the whole program finish being entered before RUN. This matters:
        # once RUN arms IRQ1V at the JAM (line 50), any leftover keystroke's CA2
        # interrupt would vector straight into the JAM and halt the machine, so
        # the keyboard must be fully idle first. Wait for the last line to echo,
        # then drain a little emulated time so no key event is still in flight.
        entered = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, "REPEAT UNTIL FALSE"),
            emulated_seconds=45.0,
        )
        assert entered, "the type-in program was not fully entered"
        bbc.run_until_or_timeout(lambda: False, emulated_seconds=3.0)

        bbc.keyboard.type("RUN\r")

        # The phrase appears once in the listing (as line 60 was typed) and
        # again when the running program PRINTs it, so >= 2 means RUN got past
        # the one-second delay in line 20 to line 60.
        ran = bbc.run_until_or_timeout(
            lambda: _count_on_screen(bbc, "HOLD BREAK") >= 2,
            emulated_seconds=10.0,
        )
        if not ran:
            rows = read_mode7_screen(bbc)
            print("\nScreen while waiting for the program to RUN:")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
        assert ran, "the type-in program did not reach its PRINT"

        _erase_screen(bbc)

        # Break with the space bar tapped while Break is held. Debugger stepping
        # (not free-run) keeps the VIA ticking while the reset line is held, and
        # the keypress latches the System VIA CA2 interrupt; on release the IRQ
        # is pending. All emulated time.
        bbc.debugger.ensure_stopped()
        assert bbc.keyboard.break_down(), "BreakDown RPC failed"
        bbc.keyboard.key_down(" ")
        _step_run(bbc, 0.05)
        assert bbc.keyboard.break_up(), "BreakUp RPC failed"
        bbc.keyboard.key_up(" ")

        recovered = _step_until(
            bbc, lambda: screen_contains(bbc, "BASIC"), emulated_seconds=3.0
        )
        if not recovered:
            rows = read_mode7_screen(bbc)
            print("\nScreen after Break (issue #78 -- no recovery):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
        assert recovered, "Banner did not return after Break (issue #78)."

        last_break = _read_byte(bbc, LAST_BREAK_TYPE)
        irq1v = _read_word(bbc, 0x0204)
        assert last_break == 0, (
            f"&028D = {last_break}, expected 0 (soft Break); the MOS reset code "
            "did not run (issue #78)."
        )
        assert irq1v != 0x0900, (
            f"IRQ1V = {irq1v:#06x}, still the planted vector; the MOS did not "
            "rebuild it (issue #78)."
        )

    def test_self_validating_detector_reports_no_irq_taken_at_reset(
        self, bbc_model_b: Beebium
    ) -> None:
        """Self-validating detector: does RESET take a pending IRQ? (issue #78)

        A short BASIC program (assembled into DIMmed memory, reset vector read
        at run time) hooks IRQ1V with a handler that, on every interrupt,
        compares the stacked return address with the reset vector's target and
        increments a marker at &70 when they match -- i.e. when an interrupt was
        taken in place of the first instruction of the reset handler -- then
        chains to the original handler. It also sets a control flag at &76
        whenever the stacked return address lies in &8000-&BFFF (BASIC running
        in a sideways ROM), which happens during the one-second wait on line 80;
        the control proves the handler's stack offsets fit this MOS and the hook
        really ran, so a broken hook cannot produce a false green. A soft Break
        preserves zero page and lets the MOS restore IRQ1V.

        Reading the pair after Break (MCS6500 Programming Manual s3.2, s9.3):
        &76 == 1 and &70 == 0 is the documented behaviour (predicted for real
        hardware): RESET sets I and the pending IRQ is masked until the MOS's own
        CLI, long after the vectors are restored. Beebium 0.1.16 measured 3, 1.
        This is the end-to-end assertion a volunteer can run on real hardware.
        """
        bbc = bbc_model_b

        program = (
            "10 REM DOES RESET TAKE A PENDING IRQ?\r"
            "20 DIM C% 80:?&70=0:?&76=0:?&71=?&204:?&72=?&205:?&74=?&FFFC:?&75=?&FFFD\r"
            "30 FOR P=0 TO 2 STEP 2:P%=C%:[OPT P\r"
            "40 .H STX &73:TSX:LDA &103,X:CMP #&80:BCC N:CMP #&C0:BCS N:LDA #1:STA &76\r"
            "50 .N LDA &103,X:CMP &75:BNE K:LDA &102,X:CMP &74:BNE K:INC &70\r"
            "60 .K LDX &73:JMP (&71)\r"
            "70 .I SEI:LDA #H MOD 256:STA &204:LDA #H DIV 256:STA &205:CLI:RTS\r"
            "80 ]:NEXT:CALL I:T%=TIME:REPEAT UNTIL TIME>T%+100\r"
            '90 PRINT "PRESS BREAK, THEN TYPE  PRINT ?&70,?&76"\r'
        )
        bbc.keyboard.type(program)
        entered = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, "?&70,?&76"),
            emulated_seconds=45.0,
        )
        assert entered, "the detector program was not fully entered"
        bbc.run_until_or_timeout(lambda: False, emulated_seconds=3.0)

        bbc.keyboard.type("RUN\r")
        # line 80 waits one second (control &76 gets set during it) before line
        # 90 prints, so >= 2 occurrences means RUN reached the PRINT.
        ran = bbc.run_until_or_timeout(
            lambda: _count_on_screen(bbc, "PRESS BREAK") >= 2,
            emulated_seconds=12.0,
        )
        assert ran, "the detector program did not RUN to its PRINT"

        # Erase the screen so the banner's return is a real signal, not the
        # stale boot banner still on screen from before.
        _erase_screen(bbc)

        # Press Break on its own (soft reset). Debugger stepping keeps the
        # free-running 100 Hz timer ticking while the reset line is held, so an
        # interrupt latches pending exactly as on real hardware; on release it is
        # pending. All emulated time, no wall-clock sleep.
        bbc.debugger.ensure_stopped()
        assert bbc.keyboard.break_down(), "BreakDown RPC failed"
        _step_run(bbc, 0.05)
        assert bbc.keyboard.break_up(), "BreakUp RPC failed"

        recovered = _step_until(
            bbc, lambda: screen_contains(bbc, "BASIC"), emulated_seconds=3.0
        )
        assert recovered, "Banner did not return after Break (issue #78)."

        control = _read_byte(bbc, 0x0076)
        marker = _read_byte(bbc, 0x0070)
        last_break = _read_byte(bbc, LAST_BREAK_TYPE)
        irq1v = _read_word(bbc, 0x0204)
        original_irq1v = _read_word(bbc, 0x0071)  # saved by line 20
        # Assert the control first: a broken hook cannot produce a false green.
        assert control == 1, (
            f"&76 = {control}, expected 1: the detector hook did not run with the "
            "assumed stack offsets, so the result is invalid, not a pass."
        )
        assert marker == 0, (
            f"&70 = {marker}: RESET took a pending IRQ in place of the reset "
            "handler's first instruction (issue #78; real hardware reports 0)."
        )
        assert last_break == 0, f"&028D = {last_break}, expected 0 (soft Break)."
        assert irq1v == original_irq1v, (
            f"IRQ1V = {irq1v:#06x}, expected the MOS default {original_irq1v:#06x}; "
            "it was not restored."
        )

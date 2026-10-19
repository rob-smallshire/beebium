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

"""Type-ahead waits for the guest to be ready, which the server reports (#125).

Keys pressed while the MOS and its ROMs are resetting are lost or read as
reset options: typing `*RUN !BOOT` 0.0, 0.1 and 0.3 emulated seconds after
power-on gave "No such variable", "File not found" and "Syntax error". The
server now holds type-ahead until the guest first removes a character from
the MOS keyboard buffer (an opcode fetch at the REMV vector's address with
X = 0), and reports it as guest_ready.
"""

from __future__ import annotations

import threading
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import DebuggerError
from beebium.client.system import ServerStatus

DISC_FILEPATH = Path(__file__).parents[3] / "tests" / "assets" / "discs" / "print_booted.ssd"
COMMAND = "*RUN !BOOT\r"


def _screen(bbc: Beebium) -> str:
    return bbc.video.screen_text().text


def _booted(bbc: Beebium) -> bool:
    return ">*RUN !BOOT" in _screen(bbc) and "BOOTED" in _screen(bbc)


def _launch_with_disc(launch_bbc, preset: str, variant: str) -> Beebium:
    bbc = launch_bbc(preset=preset, variant=variant)
    bbc.system.set_speed_multiplier(0.0)
    bbc.disc.drive(0).insert(DISC_FILEPATH)
    return bbc


@pytest.mark.parametrize(
    ("preset", "variant"),
    [
        ("model-b-disc", "model-b"),
        ("model-b-disc-65c02-copro", "model-b"),
        ("model-b-plus", "model-b-plus"),
        ("model-b-integra-b-disc", "model-b-integra-b"),
    ],
)
def test_typing_at_power_on_boots(launch_bbc, preset: str, variant: str) -> None:
    """The issue's table at 0.0 s: typed with the machine held at power-on."""
    bbc = _launch_with_disc(launch_bbc, preset, variant)
    bbc.debugger.reset()  # power-on, held before the first instruction
    assert not bbc.keyboard.typing_status().guest_ready

    bbc.keyboard.type(COMMAND)
    assert bbc.run_until_or_timeout(lambda: _booted(bbc), 5.0), _screen(bbc)
    assert bbc.keyboard.typing_status().guest_ready


def test_text_typed_during_break_is_held_through_the_reset(launch_bbc) -> None:
    bbc = _launch_with_disc(launch_bbc, "model-b-disc", "model-b")
    bbc.debugger.ensure_running()
    bbc.wait_until_ready()

    bbc.keyboard.break_down()
    assert not bbc.keyboard.typing_status().guest_ready
    bbc.keyboard.type(COMMAND)
    bbc.keyboard.break_up()
    assert bbc.run_until_or_timeout(lambda: _booted(bbc), 5.0), _screen(bbc)


def test_launch_type_wait_waits_through_the_reset(launch_bbc) -> None:
    """The ordinary sequence: the wait does not refuse a guest not yet ready."""
    bbc = _launch_with_disc(launch_bbc, "model-b-disc", "model-b")
    bbc.debugger.reset()
    bbc.keyboard.type(COMMAND)
    bbc.debugger.run()
    bbc.keyboard.wait_until_typing_complete()
    assert bbc.keyboard.typing_status().guest_ready
    assert bbc.run_until_or_timeout(lambda: _booted(bbc), 5.0), _screen(bbc)


class TestWaitUntilReady:
    def test_returns_once_the_guest_asks_for_input(self, launch_bbc) -> None:
        bbc = _launch_with_disc(launch_bbc, "model-b-disc", "model-b")
        bbc.debugger.reset()
        bbc.debugger.run()
        bbc.wait_until_ready()
        assert bbc.keyboard.typing_status().guest_ready
        # The prompt can be drawn after the last displayed frame was taken.
        assert bbc.run_until_or_timeout(lambda: ">" in _screen(bbc), 1.0), _screen(bbc)

    def test_a_ready_guest_returns_at_once(self, launch_bbc) -> None:
        bbc = _launch_with_disc(launch_bbc, "model-b-disc", "model-b")
        bbc.debugger.ensure_running()
        bbc.wait_until_ready()
        bbc.wait_until_ready()

    def test_a_stopped_machine_that_is_not_ready_raises(self, launch_bbc) -> None:
        bbc = _launch_with_disc(launch_bbc, "model-b-disc", "model-b")
        bbc.debugger.reset()
        with pytest.raises(DebuggerError, match="machine is stopped before the guest is ready"):
            bbc.wait_until_ready()

    def test_the_server_status_stream_reports_it(self, launch_bbc) -> None:
        bbc = _launch_with_disc(launch_bbc, "model-b-disc", "model-b")
        bbc.debugger.reset()
        events = bbc.system.watch_status()
        first = next(events)
        assert not first.guest_ready
        bbc.debugger.run()
        for event in events:
            if event.status == ServerStatus.GUEST_READY:
                assert event.guest_ready
                break
        events.close()


# ---------------------------------------------------------------------------
# The budget a wait gives a guest to become ready (#197)
# ---------------------------------------------------------------------------

DISCS_DIRPATH = DISC_FILEPATH.parent


def _booted_from(launch_bbc, disc_filename: str) -> Beebium:
    """A Model B that has Shift-Break booted the disc's !BOOT."""
    bbc = launch_bbc(preset="model-b-disc")
    bbc.system.set_speed_multiplier(0.0)
    bbc.debugger.ensure_running()
    bbc.wait_until_ready()
    bbc.boot_disc(DISCS_DIRPATH / disc_filename)
    return bbc


class TestReadyWithin:
    def test_a_guest_that_never_reads_the_keyboard_is_refused(self, launch_bbc) -> None:
        bbc = _booted_from(launch_bbc, "never_reads_keyboard.ssd")
        bbc.keyboard.type("PRINT 1\r")
        with pytest.raises(
            DebuggerError,
            match=r"guest has not read the keyboard since reset \(5\.0 emulated s\); "
                  r"type-ahead is held with 8 characters pending -- programs that scan "
                  r"the matrix directly take key_down/key_up, not type\(\)",
        ):
            bbc.keyboard.wait_until_typing_complete()
        status = bbc.keyboard.typing_status()
        assert not status.guest_ready
        assert status.pending_characters == 8
        assert status.cycles_since_reset > 5.0 * 2_000_000

    def test_no_budget_waits_until_cancelled(self, launch_bbc) -> None:
        bbc = _booted_from(launch_bbc, "never_reads_keyboard.ssd")
        bbc.keyboard.type("PRINT 1\r")
        outcome: list[object] = []

        def wait() -> None:
            try:
                bbc.keyboard.wait_until_typing_complete(ready_within=None)
                outcome.append("returned")
            except Exception as e:  # noqa: BLE001 -- recorded for the assertion
                outcome.append(e)

        waiter = threading.Thread(target=wait)
        waiter.start()
        waiter.join(timeout=3.0)  # many emulated seconds at unlimited speed
        assert waiter.is_alive(), f"the wait ended: {outcome}"
        assert bbc.keyboard.typing_status().cycles_since_reset > 5.0 * 2_000_000
        bbc.keyboard.clear_typing()  # the only way it ends
        waiter.join(timeout=10.0)
        assert outcome == ["returned"]

    def test_a_slow_boot_completes_within_a_larger_budget(self, launch_bbc) -> None:
        bbc = _booted_from(launch_bbc, "slow_boot.ssd")
        bbc.keyboard.type("PRINT 6*7\r")
        bbc.keyboard.wait_until_typing_complete(ready_within=30.0)
        assert bbc.keyboard.typing_status().guest_ready
        assert bbc.run_until_or_timeout(lambda: "42" in _screen(bbc), 5.0), _screen(bbc)

    def test_a_slow_boot_outlasts_the_default_budget(self, launch_bbc) -> None:
        bbc = _booted_from(launch_bbc, "slow_boot.ssd")
        bbc.keyboard.type("PRINT 6*7\r")
        with pytest.raises(DebuggerError, match="guest has not read the keyboard"):
            bbc.keyboard.wait_until_typing_complete()

    def test_wait_until_ready_takes_the_same_budget(self, launch_bbc) -> None:
        bbc = _booted_from(launch_bbc, "never_reads_keyboard.ssd")
        with pytest.raises(DebuggerError, match=r"\(1\.0 emulated s\)"):
            bbc.wait_until_ready(ready_within=1.0)
        slow = _booted_from(launch_bbc, "slow_boot.ssd")
        slow.wait_until_ready(ready_within=30.0)


# ---------------------------------------------------------------------------
# A reset discards the type-ahead it interrupts (#206)
# ---------------------------------------------------------------------------

FIRST_LINE = "PRINT 12345\r"
SECOND_LINE = "PRINT 67890\r"
SECONDS_PER_CHARACTER = 0.08  # 40 ms down, 40 ms up


def _at_prompt(bbc: Beebium) -> bool:
    lines = [line.rstrip() for line in _screen(bbc).splitlines() if line.strip()]
    return bool(lines) and lines[-1] == ">"


def _reset_by_break(bbc: Beebium) -> None:
    bbc.debugger.run()
    bbc.keyboard.press_break()


def _reset_by_ctrl_break(bbc: Beebium) -> None:
    bbc.debugger.run()
    bbc.keyboard.ctrl_break()


def _reset_by_debugger(bbc: Beebium) -> None:
    bbc.debugger.reset()


@pytest.mark.parametrize(
    "reset", [_reset_by_break, _reset_by_ctrl_break, _reset_by_debugger],
    ids=["break", "ctrl-break", "debugger-reset"],
)
def test_a_reset_discards_the_line_being_typed_and_those_behind_it(launch_bbc, reset) -> None:
    bbc = launch_bbc(preset="model-b-disc")
    bbc.system.set_speed_multiplier(0.0)
    bbc.wait_until_ready()
    bbc.debugger.ensure_stopped()

    bbc.keyboard.type(FIRST_LINE)
    bbc.keyboard.type(SECOND_LINE)
    bbc.run_for_emulated_seconds(5 * SECONDS_PER_CHARACTER)  # "PRINT" in, " 12345" not
    pending = bbc.keyboard.typing_status().pending_characters
    assert pending > len(SECOND_LINE)

    reset(bbc)
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc), 5.0), _screen(bbc)
    bbc.run_for_emulated_seconds(len(FIRST_LINE + SECOND_LINE) * SECONDS_PER_CHARACTER)

    screen = _screen(bbc)
    assert "12345" not in screen and "67890" not in screen and "Mistake" not in screen, screen
    status = bbc.keyboard.typing_status()
    assert status.idle
    # A Break is pressed on a running machine, which may type a little more first.
    assert len(SECOND_LINE) < status.discarded_by_reset <= pending
    with pytest.raises(DebuggerError, match=f"a reset discarded {status.discarded_by_reset} characters"):
        bbc.keyboard.wait_until_typing_complete()


def test_text_typed_after_the_reset_is_typed_into_the_new_session(launch_bbc) -> None:
    bbc = launch_bbc(preset="model-b-disc")
    bbc.system.set_speed_multiplier(0.0)
    bbc.wait_until_ready()
    bbc.keyboard.type(FIRST_LINE)
    bbc.keyboard.press_break()

    bbc.keyboard.type("PRINT 6*7\r")
    assert bbc.keyboard.typing_status().discarded_by_reset == 0
    bbc.keyboard.wait_until_typing_complete()
    assert bbc.run_until_or_timeout(lambda: "42" in _screen(bbc), 5.0), _screen(bbc)
    assert "12345" not in _screen(bbc)

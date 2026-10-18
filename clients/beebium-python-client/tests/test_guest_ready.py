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
        assert ">" in _screen(bbc)

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

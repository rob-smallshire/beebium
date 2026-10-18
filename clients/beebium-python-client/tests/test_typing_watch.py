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

"""The server reports when typing finishes; callers must not have to infer it.

Polling and inferring completion needs a timeout, and no timeout is right for
every paste: throughput depends on how fast the host can emulate and on the
text itself, since every capital costs an extra SHIFT press.
"""

from __future__ import annotations

import threading
import time

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import DebuggerError

# Generous for a refusal that should be immediate, far short of a hang.
_PROMPT_SECONDS = 5.0


class TestWatchTypingStatus:
    """Streaming type-ahead status."""

    def test_first_update_arrives_without_waiting_for_a_change(self, bbc: Beebium) -> None:
        """An initial snapshot means a late subscriber is not left hanging."""
        bbc.keyboard.clear_typing()
        updates = bbc.keyboard.watch_typing_status()
        first = next(updates)
        updates.close()
        assert first.idle
        assert first.pending_characters == 0

    def test_status_says_whether_the_machine_is_running(self, bbc: Beebium) -> None:
        """The server reports the run state; the client does not infer it."""
        bbc.debugger.ensure_running()
        assert bbc.keyboard.typing_status().machine_running
        bbc.debugger.stop()
        assert not bbc.keyboard.typing_status().machine_running

    def test_a_stop_is_pushed_though_the_queue_is_unchanged(self, bbc: Beebium) -> None:
        bbc.debugger.ensure_running()
        bbc.keyboard.clear_typing()
        updates = bbc.keyboard.watch_typing_status()
        assert next(updates).machine_running
        bbc.debugger.stop()
        stopped = next(updates)
        updates.close()
        assert not stopped.machine_running
        assert stopped.idle

    def test_pending_count_falls_as_characters_are_typed(self, bbc: Beebium) -> None:
        bbc.keyboard.clear_typing()
        bbc.keyboard.type("PRINT 1\r" * 3)

        seen = []
        for status in bbc.keyboard.watch_typing_status():
            seen.append(status.pending_characters)
            if status.idle:
                break

        assert seen[0] > 0, "started with characters pending"
        assert seen[-1] == 0, "ended with none"
        assert seen == sorted(seen, reverse=True), "never went up"


class TestWaitUntilTypingComplete:
    """Blocking until the server says the queue has drained."""

    def test_returns_when_typing_finishes(self, bbc: Beebium) -> None:
        bbc.keyboard.clear_typing()
        bbc.keyboard.type("PRINT 2\r" * 4)
        bbc.keyboard.wait_until_typing_complete()
        assert bbc.keyboard.typing_status().idle
        assert bbc.keyboard.typing_status().pending_characters == 0

    def test_returns_immediately_when_nothing_is_queued(self, bbc: Beebium) -> None:
        bbc.keyboard.clear_typing()
        bbc.keyboard.wait_until_typing_complete()

    def test_returns_when_the_queue_is_cleared_from_another_thread(
        self, bbc: Beebium
    ) -> None:
        """Cancelling a paste must end the wait, not strand the caller."""
        bbc.keyboard.clear_typing()
        bbc.keyboard.type("PRINT 3\r" * 200)

        cleared: list[int] = []
        canceller = threading.Timer(1.0, lambda: cleared.append(bbc.keyboard.clear_typing()))
        canceller.start()
        try:
            bbc.keyboard.wait_until_typing_complete()
        finally:
            canceller.cancel()

        assert cleared and cleared[0] > 0, "cancelled while characters were pending"
        assert bbc.keyboard.typing_status().idle

    def test_a_stopped_machine_with_typing_pending_raises_at_once(self, bbc: Beebium) -> None:
        """The #184 sequence: a run leaves the machine stopped, then a wait."""
        bbc.run_for_emulated_seconds(0.5)
        bbc.keyboard.type("PRINT 4\r")

        started = time.monotonic()
        with pytest.raises(DebuggerError, match="machine is stopped.*debugger.run"):
            bbc.keyboard.wait_until_typing_complete()
        assert time.monotonic() - started < _PROMPT_SECONDS
        assert bbc.keyboard.typing_status().pending_characters > 0, "nothing was typed"

    def test_a_machine_stopped_part_way_through_raises(self, bbc: Beebium) -> None:
        bbc.debugger.ensure_running()
        bbc.keyboard.clear_typing()
        bbc.keyboard.type("PRINT 5\r" * 200)

        stopper = threading.Timer(1.0, bbc.debugger.stop)
        stopper.start()
        started = time.monotonic()
        try:
            with pytest.raises(DebuggerError, match="machine is stopped"):
                bbc.keyboard.wait_until_typing_complete()
        finally:
            stopper.cancel()
        assert time.monotonic() - started < 1.0 + _PROMPT_SECONDS

    def test_a_stopped_machine_with_nothing_pending_returns(self, bbc: Beebium) -> None:
        bbc.keyboard.clear_typing()
        bbc.debugger.stop()
        bbc.keyboard.wait_until_typing_complete()


class TestExpectOnAStoppedMachine:
    """expect() names the real cause rather than running out its timeout."""

    def test_a_stopped_machine_raises_at_once(self, bbc: Beebium) -> None:
        bbc.run_for_emulated_seconds(0.5)
        started = time.monotonic()
        with pytest.raises(DebuggerError, match="machine is stopped.*debugger.run"):
            bbc.expect("NOT ON THE SCREEN", timeout=60.0)
        assert time.monotonic() - started < _PROMPT_SECONDS

    def test_a_machine_stopped_while_waiting_raises(self, bbc: Beebium) -> None:
        bbc.debugger.ensure_running()
        stopper = threading.Timer(1.0, bbc.debugger.stop)
        stopper.start()
        started = time.monotonic()
        try:
            with pytest.raises(DebuggerError, match="machine is stopped"):
                bbc.expect("NOT ON THE SCREEN", timeout=60.0, sample_interval_seconds=0.2)
        finally:
            stopper.cancel()
        assert time.monotonic() - started < 1.0 + _PROMPT_SECONDS

    def test_text_already_on_a_stopped_machine_matches(self, bbc: Beebium) -> None:
        bbc.debugger.ensure_running()
        bbc.expect("BASIC", timeout=30.0, sample_interval_seconds=0.2)
        bbc.debugger.stop()
        assert bbc.expect("BASIC", timeout=1.0) == "BASIC"

    def test_a_running_machine_still_waits_for_its_text(self, bbc: Beebium) -> None:
        bbc.debugger.ensure_running()
        bbc.expect(">", timeout=30.0, sample_interval_seconds=0.2)
        bbc.keyboard.type("PRINT 6*7\r")
        assert bbc.expect("42", timeout=30.0, sample_interval_seconds=0.2) == "42"

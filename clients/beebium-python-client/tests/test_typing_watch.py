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

from beebium.client import Beebium


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

    def test_status_unpacks_as_a_tuple(self, bbc: Beebium) -> None:
        """The named fields are additive; existing unpacking still works."""
        idle, pending, queued = bbc.keyboard.typing_status()
        assert isinstance(idle, bool)
        assert (idle, pending, queued) == tuple(bbc.keyboard.typing_status())

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

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

"""WatchServerStatus emits a MACHINE_RESET event on every reset.

A client that mirrors machine state (e.g. the macOS keyboard-lock mirror) needs
to know when the emulated machine has been reset, however initiated, so it can
resync the same way it does at boot. The server surfaces this on
SystemService.WatchServerStatus as a SERVER_STATUS_MACHINE_RESET event, carrying
the reset kind: SOFT for the Break key, HARD for a Reset RPC / Ctrl-Break.
"""

from __future__ import annotations

import queue
import threading

import pytest

from beebium.client.system import ResetKind, ServerStatus


def _collect_reset_events(bbc, out: queue.Queue, stop: threading.Event) -> None:
    """Stream WatchServerStatus in a thread, queueing MACHINE_RESET events.

    The stream also carries ~2 Hz heartbeats; we forward only resets. The loop
    ends when the stop flag is set (the test drops the connection) and the
    generator raises, which we swallow.
    """
    try:
        for event in bbc.system.watch_status():
            if event.status == ServerStatus.MACHINE_RESET:
                out.put(event)
            if stop.is_set():
                return
    except Exception:  # noqa: BLE001 - the stream ends on teardown; that's fine
        return


def _next_reset(out: queue.Queue, timeout: float = 15.0):
    try:
        return out.get(timeout=timeout)
    except queue.Empty:
        return None


def test_break_and_reset_emit_machine_reset_events(bbc) -> None:
    events: queue.Queue = queue.Queue()
    stop = threading.Event()
    watcher = threading.Thread(
        target=_collect_reset_events, args=(bbc, events, stop), daemon=True
    )
    watcher.start()
    try:
        # A reset that happened before the watcher subscribed must not be
        # replayed, so drain anything already queued from startup.
        while _next_reset(events, timeout=0.5) is not None:
            pass

        # The Break key is a soft reset.
        bbc.keyboard.shift_break()
        soft = _next_reset(events)
        assert soft is not None, "no MACHINE_RESET event after Break"
        assert soft.status == ServerStatus.MACHINE_RESET
        assert soft.reset_kind == ResetKind.SOFT

        # A Reset RPC is a hard reset.
        bbc.debugger.reset()
        hard = _next_reset(events)
        assert hard is not None, "no MACHINE_RESET event after Reset RPC"
        assert hard.reset_kind == ResetKind.HARD
    finally:
        stop.set()

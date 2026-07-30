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

"""Tier 2, scenario 4: repeated transactions against a real bridge.

A fileserver acknowledges our command and then replies as a fresh
transaction. Those are two datagrams, and nothing guarantees they arrive in
the order they were sent. When the reply overtakes its own ack, it lands while
the handshake is still waiting for that ack -- the window in which
``FourWayHandshake`` used to destroy it, leaving NFS waiting for a response
that no longer existed.

One iteration proves nothing: the reordering is load-dependent. This loops, and
reports the holding queue's counters at the end so a run can be told apart from
one in which no reordering happened to occur. It also asserts that nothing was
dropped for want of queue space and nothing expired unredelivered -- either
would mean traffic was lost even with the queue in place.

**Observed:** on a loopback path to a containerised bridge, 25 iterations
produce `held=0 redelivered=0 expired=0 dropped=0` -- no reordering occurs at
all, so this scenario does not currently exercise the holding queue. That is
not a reason to delete it (it is still a real multi-transaction interop test)
but it is the reason a perturbation proxy is needed: only deliberate,
controlled reordering will make defect 1 reproducible on demand. See
docs/discussion/pieconetbridge-aun-interop-testing.md, "Reproducing the
defects".

The deterministic proof that the queue works lives in the unit tests tagged
`[holding]` in tests/test_four_way_handshake.cpp.

See docs/discussion/aun-robustness.md defect 1.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen

from pieb_test_support.topology import BRIDGE_FS_STATION, BRIDGE_NET

# Enough iterations to give reordering a chance without turning a routine run
# into a coffee break. Raise it when chasing a suspected ordering bug.
ITERATIONS = 25

ECONET_FAILURES = ("No reply", "Not listening", "No clock", "Net error",
                   "not present", "not listening")


def _wait_for_boot_prompt(bbc, timeout_seconds=30.0):
    """Wait for the machine to reach a BASIC prompt after boot."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        if any(row.strip() == ">" for row in rows):
            return True
        time.sleep(0.2)
    return False


def _wait_for_command_outcome(bbc, command_text, timeout_seconds=45.0):
    """Wait for a typed command to finish, and report how it went.

    Returns None on success, or the Econet error text that appeared.

    Waiting for "the screen ends with a prompt" is not enough: the prompt from
    *before* the command is still there the instant it is typed, so a naive
    check returns immediately and the caller races on to the next command
    without any traffic having happened. This looks for a prompt on a line
    *after* the command's own echo, which only appears once the command has
    actually completed.
    """
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        screen = "\n".join(rows)
        for failure in ECONET_FAILURES:
            if failure in screen:
                return failure

        seen_command = False
        for row in rows:
            stripped = row.strip()
            if command_text in stripped:
                seen_command = True
            elif seen_command and stripped == ">":
                return None
        time.sleep(0.2)
    return f"timed out waiting for {command_text!r} to complete"


@pytest.mark.slow
@pytest.mark.timeout(900)
def test_repeated_login_and_catalogue(
    bridge, beebium_args, server_filepath, mos_filepath, basic_filepath,
):
    """Log in and catalogue repeatedly; every iteration must complete."""
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=beebium_args,
        startup_timeout=30.0,
    ) as bbc:
        assert _wait_for_boot_prompt(bbc), dump_screen(bbc)
        bbc.keyboard.type("*NET\r")
        assert _wait_for_command_outcome(bbc, "*NET") is None, dump_screen(bbc)

        for iteration in range(ITERATIONS):
            bbc.keyboard.type(f"*I AM {BRIDGE_NET}.{BRIDGE_FS_STATION} SYST\r")
            failure = _wait_for_command_outcome(bbc, "*I AM")
            assert failure is None, (
                f"Login failed on iteration {iteration} with {failure!r}:\n"
                f"{dump_screen(bbc)}"
            )

            bbc.keyboard.type("*CAT\r")
            failure = _wait_for_command_outcome(bbc, "*CAT")
            assert failure is None, (
                f"*CAT failed on iteration {iteration} with {failure!r}:\n"
                f"{dump_screen(bbc)}"
            )

            bbc.keyboard.type("*BYE\r")
            failure = _wait_for_command_outcome(bbc, "*BYE")
            assert failure is None, (
                f"*BYE failed on iteration {iteration} with {failure!r}:\n"
                f"{dump_screen(bbc)}"
            )

        handshake = bbc.econet.status.handshake
        assert handshake is not None, "No handshake status reported"
        print(
            f"\nHolding queue after {ITERATIONS} iterations: "
            f"held={handshake.frames_held} "
            f"redelivered={handshake.frames_redelivered} "
            f"expired={handshake.frames_expired} "
            f"dropped={handshake.frames_dropped}"
        )

        # Redelivery is not asserted: whether any packet arrived out of order
        # depends on load and timing, and demanding it would make this flaky.
        # What must hold is that the queue never lost anything -- a drop means
        # it overflowed, an expiry means a frame waited out its TTL with no
        # stage ever willing to take it.
        assert handshake.frames_dropped == 0, (
            f"{handshake.frames_dropped} frames dropped: the holding queue "
            "overflowed, so traffic was lost despite the queue"
        )
        assert handshake.frames_expired == 0, (
            f"{handshake.frames_expired} frames expired unredelivered: held "
            "traffic was never accepted by any stage"
        )

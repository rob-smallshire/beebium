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

One iteration proves nothing: the reordering is load-dependent. This loops.

It asserts only on the outcome, which makes it a weaker test than it should be:
a run in which no reordering happened to occur passes identically to one in
which the holding queue saved every reply. `FourWayHandshake` counts frames
held, redelivered, expired and dropped, but those counters are not yet
reachable from a client. Once they are, this test should assert the
redelivery count is non-zero, so that it cannot pass vacuously. Until then the
deterministic proof lives in the unit tests tagged `[holding]` in
tests/test_four_way_handshake.cpp, and this scenario is a smoke test against a
real peer.

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

ECONET_FAILURES = ("No reply", "Not listening", "No clock", "Net error")


def _wait_for_quiet_prompt(bbc, timeout_seconds=45.0):
    """Wait for a prompt, failing fast on any Econet error message."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        screen = "\n".join(read_mode7_screen(bbc))
        for failure in ECONET_FAILURES:
            if failure in screen:
                return failure
        if screen.rstrip().endswith(">"):
            return None
        time.sleep(0.2)
    return "timed out waiting for a prompt"


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
        assert _wait_for_quiet_prompt(bbc) is None, dump_screen(bbc)
        bbc.keyboard.type("*NET\r")
        assert _wait_for_quiet_prompt(bbc) is None, dump_screen(bbc)

        for iteration in range(ITERATIONS):
            bbc.keyboard.type(f"*I AM {BRIDGE_NET}.{BRIDGE_FS_STATION} SYST\r")
            failure = _wait_for_quiet_prompt(bbc)
            assert failure is None, (
                f"Login failed on iteration {iteration} with {failure!r}:\n"
                f"{dump_screen(bbc)}"
            )

            bbc.keyboard.type("*CAT\r")
            failure = _wait_for_quiet_prompt(bbc)
            assert failure is None, (
                f"*CAT failed on iteration {iteration} with {failure!r}:\n"
                f"{dump_screen(bbc)}"
            )

            bbc.keyboard.type("*BYE\r")
            failure = _wait_for_quiet_prompt(bbc)
            assert failure is None, (
                f"*BYE failed on iteration {iteration} with {failure!r}:\n"
                f"{dump_screen(bbc)}"
            )

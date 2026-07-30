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

"""Tier 2, scenario 4: a fileserver reply that overtakes its own ack.

A fileserver acknowledges a command and then replies as a fresh transaction.
Those are two datagrams, and nothing guarantees they arrive in the order they
were sent. When the reply overtakes its ack it lands while the handshake is
still waiting for that ack -- the window in which ``FourWayHandshake`` used to
destroy it, leaving NFS waiting for a response that no longer existed.

The plain repeated-login scenario cannot reach this: 25 iterations against a
real bridge on loopback produce a holding-queue count of exactly zero, because
nothing reorders by itself. So the ordering is imposed deliberately, through a
proxy that leaves the traffic genuine and only changes when it arrives.

See docs/discussion/aun-robustness.md defect 1.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen

from pieb_test_support.perturb import AunType, Direction
from pieb_test_support.topology import BRIDGE_FS_STATION, BRIDGE_NET

ECONET_FAILURES = ("No reply", "Not listening", "No clock", "Net error",
                   "not present", "not listening")


def _diagnostics(bbc, proxy):
    """Instrument state to accompany a failure.

    A failure here has two possible authors -- the stack under test and the
    perturbation itself -- and without the proxy's counters there is no way to
    tell which one to look at.
    """
    counters = proxy.counters
    handshake = bbc.econet.status.handshake
    queue = ("none reported" if handshake is None else
             f"held={handshake.frames_held} "
             f"redelivered={handshake.frames_redelivered} "
             f"expired={handshake.frames_expired} "
             f"dropped={handshake.frames_dropped}")
    return (f"proxy: forwarded={counters.forwarded} held={counters.held} "
            f"released={counters.released} expired_holds={counters.expired_holds}\n"
            f"holding queue: {queue}")


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
@pytest.mark.timeout(600)
@pytest.mark.xfail(
    strict=True,
    reason=(
        "Reproduces a residual defect the proxy was built to find. With every "
        "acknowledgement held one datagram back, the login succeeds and *CAT "
        "prints its full catalogue, but the transaction never completes and "
        "the prompt does not return. The perturbation is doing real work and "
        "is not deadlocking: all holds are released by traffic overtaking "
        "them (expired_holds=0), and the holding queue is demonstrably active "
        "(redelivered=1). Whether the cause is in Beebium or in how the "
        "bridge reacts to a delayed ack is not yet established. See "
        "docs/discussion/aun-robustness.md."
    ),
)
def test_reply_overtaking_its_ack_still_reaches_the_guest(
    bridge, perturbing_proxy, beebium_args_via_proxy,
    server_filepath, mos_filepath, basic_filepath,
):
    """Hold each ack one datagram back, so every reply arrives early."""
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=beebium_args_via_proxy,
        startup_timeout=30.0,
    ) as bbc:
        assert _wait_for_boot_prompt(bbc), dump_screen(bbc)
        bbc.keyboard.type("*NET\r")
        assert _wait_for_command_outcome(bbc, "*NET") is None, dump_screen(bbc)

        # From here on, every acknowledgement the bridge sends is held until
        # one further datagram has passed it -- which is the fileserver's
        # reply. Renewed each iteration because a rule is spent when it fires.
        for iteration in range(6):
            # Hold each acknowledgement until one further datagram has
            # overtaken it, or 250ms passes -- whichever comes first. The
            # deadline matters: an ack that happens to be the last datagram of
            # a transaction has nothing behind it to let past, and holding it
            # indefinitely would be loss rather than reordering.
            perturbing_proxy.hold_next(
                Direction.INBOUND, types=(AunType.ACK,), after=1, uses=4,
                max_hold_seconds=0.25)

            bbc.keyboard.type(f"*I AM {BRIDGE_NET}.{BRIDGE_FS_STATION} SYST\r")
            failure = _wait_for_command_outcome(bbc, "*I AM")
            assert failure is None, (
                f"Login failed on iteration {iteration} with {failure!r} "
                f"while replies were arriving before their acks:\n"
                f"{_diagnostics(bbc, perturbing_proxy)}\n{dump_screen(bbc)}"
            )

            bbc.keyboard.type("*CAT\r")
            failure = _wait_for_command_outcome(bbc, "*CAT")
            assert failure is None, (
                f"*CAT failed on iteration {iteration} with {failure!r}:\n"
                f"{_diagnostics(bbc, perturbing_proxy)}\n{dump_screen(bbc)}"
            )

            bbc.keyboard.type("*BYE\r")
            assert _wait_for_command_outcome(bbc, "*BYE") is None, dump_screen(bbc)

        perturbing_proxy.clear_rules()

        proxy_counters = perturbing_proxy.counters
        handshake = bbc.econet.status.handshake
        assert handshake is not None, "No handshake status reported"

        print(
            f"\nProxy: held={proxy_counters.held} "
            f"released={proxy_counters.released} "
            f"forwarded={proxy_counters.forwarded}\n"
            f"Holding queue: held={handshake.frames_held} "
            f"redelivered={handshake.frames_redelivered} "
            f"expired={handshake.frames_expired} "
            f"dropped={handshake.frames_dropped}"
        )

        # The instrument must have done its job, or this test passed for the
        # wrong reason -- which is precisely the failure mode the plain
        # repeated-login scenario suffers from.
        assert proxy_counters.held > 0, (
            "The proxy never held an ack, so no reordering was imposed and "
            "this scenario proved nothing"
        )

        # And the stack must have exercised the holding queue rather than
        # merely surviving. Without it the reordered replies would have been
        # destroyed and every login above would have failed.
        assert handshake.frames_redelivered > 0, (
            "Replies arrived before their acks but nothing was ever held and "
            "redelivered, so the holding queue was not what carried them"
        )
        assert handshake.frames_dropped == 0, (
            f"{handshake.frames_dropped} frames dropped: the holding queue "
            "overflowed under reordering"
        )

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

"""Tier 1, scenario 1: a Beebium station logs in to a PiEconetBridge fileserver.

The baseline the rest of the interop programme is measured against, and the
first automated evidence that Beebium's synthesised four-way handshake
satisfies an independently written AUN implementation rather than only
satisfying another Beebium.

The same scenario runs twice, differing only in what Beebium declares with
``--aun net=``: the flat-cloud declaration of net 0, and a declaration matching
the non-zero net the bridge knows us by. The second was the reproducer for
defect 8 and is kept as its regression test.
See docs/discussion/pieconetbridge-aun-interop-testing.md.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen

from pieb_test_support.topology import (
    BEEBIUM_BRIDGE_SIDE_NET,
    BRIDGE_FS_STATION,
    BRIDGE_NET,
)


def _wait_for_screen_text(bbc, text, timeout_seconds=30.0, poll_interval=0.25):
    """Wait until the given text appears anywhere on the MODE 7 screen."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if text in "\n".join(read_mode7_screen(bbc)):
            return True
        time.sleep(poll_interval)
    return False


def _log_in_and_catalogue(bbc):
    """Boot, log in to the bridge's fileserver, and catalogue its disc."""
    assert _wait_for_screen_text(bbc, "Econet Station"), \
        f"No Econet station banner -- hardware not fitted?\n{dump_screen(bbc)}"
    assert _wait_for_screen_text(bbc, ">"), \
        f"Did not reach the BASIC prompt:\n{dump_screen(bbc)}"

    # The peer map and the carrier are prerequisites for everything below.
    # Checking them separately keeps a configuration mistake from being
    # reported as a protocol failure.
    assert bbc.econet.status.connected, \
        "Econet reports no carrier before any traffic"
    active = bbc.transport.active
    assert active is not None and active.name == "aun", \
        f"Expected the AUN transport to be active, got {active}"

    bbc.keyboard.type("*NET\r")
    assert _wait_for_screen_text(bbc, ">"), \
        f"*NET did not return to a prompt:\n{dump_screen(bbc)}"

    # The fileserver initialises its own Passwords file with a privileged SYST
    # user and a blank password when the filestore starts empty, so there is
    # no credential setup to do.
    bbc.keyboard.type(f"*I AM {BRIDGE_NET}.{BRIDGE_FS_STATION} SYST\r")

    # A failed login is not silent for long: NFS gives up with a diagnosable
    # message. Fail on it as soon as it appears rather than waiting out the
    # timeout on the *CAT that follows.
    deadline = time.monotonic() + 45.0
    while time.monotonic() < deadline:
        screen = "\n".join(read_mode7_screen(bbc))
        for failure in ("No reply", "Not listening", "No clock", "Net error"):
            if failure in screen:
                pytest.fail(f"Login failed with {failure!r}:\n{dump_screen(bbc)}")
        if screen.rstrip().endswith(">"):
            break
        time.sleep(0.25)

    # *CAT is the cheapest proof that the session is real: it needs a complete
    # four-way exchange in each direction after the login, and it names the
    # bridge's disc, which only the fileserver can have told us.
    bbc.keyboard.type("*CAT\r")
    assert _wait_for_screen_text(bbc, "BEEBIUM", timeout_seconds=30), \
        f"*CAT did not show the bridge's disc:\n{dump_screen(bbc)}"


@pytest.mark.slow
@pytest.mark.timeout(180)
def test_login_with_flat_net_declaration(
    bridge, beebium_args, server_filepath, mos_filepath, basic_filepath,
):
    """The flat-cloud arrangement: Beebium declares net 0.

    The bridge knows us by a non-zero net because its configuration demands
    one, but we describe ourselves to the guest as being on net 0 -- "this
    segment" -- which is the form BBC software expects.
    """
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=beebium_args,
        startup_timeout=30.0,
    ) as bbc:
        _log_in_and_catalogue(bbc)


@pytest.mark.slow
@pytest.mark.timeout(180)
@pytest.mark.parametrize("beebium_net", [BEEBIUM_BRIDGE_SIDE_NET], indirect=True)
def test_login_with_matching_non_zero_net_declaration(
    bridge, beebium_args, server_filepath, mos_filepath, basic_filepath,
):
    """The honest multi-net arrangement: Beebium declares the net it is on.

    Identical to the flat-cloud case in every respect except ``--aun net=``,
    which here matches the net the bridge knows us by. This is precisely the
    configuration ``--aun net=N`` exists to support.

    It did not work until defect 8 was fixed: inbound frames were delivered
    with ``dest_net`` set to our declared net, which the guest -- which can
    only ever believe it is on net 0 -- discarded as not addressed to it. See
    docs/discussion/aun-robustness.md.
    """
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=beebium_args,
        startup_timeout=30.0,
    ) as bbc:
        _log_in_and_catalogue(bbc)

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

"""Tier 2, scenario 7: transmitting to a station that is not there.

The four-way handshake synthesises its scout acknowledgement before anything
has left the machine. Without a reachability check the guest is told its scout
was answered by a station that does not exist, and OSWORD &10 reports &00
"Transmitted OK" for a frame that went nowhere.

A guest talking to absent hardware must be told so. What it must never be told
is that the transmission succeeded.

See docs/discussion/aun-robustness.md defect 2.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.screen import dump_screen, read_mode7_screen

from pieb_test_support.topology import BRIDGE_NET

# A station on the bridge's net that neither the bridge nor our peer table
# knows anything about.
ABSENT_STATION = 99

# Any of these is an acceptable outcome: the guest has been told the truth.
# Which one appears depends on how far the ROM gets before giving up.
FAILURE_MESSAGES = ("Not listening", "No reply", "Net error", "Line jammed")


@pytest.mark.slow
@pytest.mark.timeout(300)
@pytest.mark.xfail(
    strict=True,
    reason=(
        "Defect 2 is open. The synthetic final ack is unconditional, so a "
        "transmission that went nowhere reports success. Two attempted fixes "
        "were reverted after this test showed they made matters worse -- see "
        "docs/discussion/aun-robustness.md defect 2 for what was tried."
    ),
)
def test_transmission_to_absent_station_is_reported_as_failure(
    bridge, beebium_args, server_filepath, mos_filepath, basic_filepath,
):
    """*I AM to a station nobody knows must fail, and must fail visibly."""
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=server_filepath,
        extra_args=beebium_args,
        startup_timeout=30.0,
    ) as bbc:
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            if "\n".join(read_mode7_screen(bbc)).rstrip().endswith(">"):
                break
            time.sleep(0.2)

        bbc.keyboard.type("*NET\r")
        time.sleep(1.0)
        bbc.keyboard.type(f"*I AM {BRIDGE_NET}.{ABSENT_STATION} SYST\r")

        # NFS applies generous timeouts to an OSWORD, so allow well over a
        # second, but the failure must arrive rather than the command quietly
        # appearing to succeed.
        deadline = time.monotonic() + 60.0
        screen = ""
        while time.monotonic() < deadline:
            screen = "\n".join(read_mode7_screen(bbc))
            if any(message in screen for message in FAILURE_MESSAGES):
                return
            time.sleep(0.25)

        pytest.fail(
            "Transmission to an absent station produced no error. The guest "
            "was told the frame was sent when nothing left the machine.\n"
            f"{dump_screen(bbc)}"
        )

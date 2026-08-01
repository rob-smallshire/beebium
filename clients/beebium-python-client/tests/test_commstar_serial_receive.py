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

"""Characters received by real comms software, after the line has been abused.

Issue #59: dialling a BBS through tcpser, the first character received after an
idle line was sometimes not displayed, though the trace showed it reaching the
BBC. The cause was an ACIA receiver overrun left standing from an earlier burst
that nobody read -- a modem's `OK` or `NO CARRIER` arriving before the guest had
configured the serial port. The next character to arrive inherited that error
flag and the MOS discarded it.

These tests drive Commstar over `rpc-serial`, which delivers bytes to the ACIA
exactly as a modem would but under the test's control, and read back what the
guest actually displayed.
"""

import time
from pathlib import Path

import pytest
from conftest import enter_prestel_chat, screen, transmitted, wait_for_screen

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.ext.peripheral.rpc_serial import RpcSerial

PROBE = b"ABCDE"

# A modem hanging up talks to nobody in particular. This is what tcpser emits.
STALE_BURST = b"\r\nOK\r\n"


@pytest.fixture
def commstar_after_stale_burst(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    commstar_rom_filepath: Path,
):
    """Commstar in Prestel chat, having been preceded by an unread serial burst.

    The burst is delivered while the machine is still at the BASIC prompt, so
    the ACIA has not been configured by Commstar and nothing drains the receive
    register: the receiver overruns before the session even begins.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--rpc-serial",
                "--sideways",
                f"13:rom:{commstar_rom_filepath}",
            ],
        ) as bbc:
            wait_for_screen(bbc, "BASIC")
            bbc.extensions[RpcSerial].send(STALE_BURST)
            time.sleep(1.0)
            enter_prestel_chat(bbc)
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _displayed(bbc) -> str:
    """Everything on screen, whitespace collapsed, for substring assertions."""
    return "".join(line.strip() for line in screen(bbc).splitlines())


def _receive(bbc, data: bytes) -> None:
    """Deliver bytes to the BBC's serial port and let the ULA clock them in."""
    accepted = bbc.extensions[RpcSerial].send(data)
    assert accepted == len(data), f"rpc-serial accepted {accepted} of {len(data)}"
    # Viewdata receives at 1200 baud: ~8ms a byte, plus the guest's own work.
    time.sleep(2.0)


def test_received_characters_are_displayed(commstar_prestel_bbc):
    """The baseline: bytes arriving in chat mode appear on screen."""
    bbc = commstar_prestel_bbc
    transmitted(bbc)

    _receive(bbc, PROBE)

    assert PROBE.decode() in _displayed(bbc)


def test_a_burst_nobody_read_does_not_eat_the_next_character(
    commstar_after_stale_burst,
):
    """Issue #59: a stale overrun must not consume a later, unrelated character.

    The burst is delivered before Commstar has configured the ACIA, so nothing
    reads it and the receiver overruns -- exactly as when tcpser reports `OK`
    to a machine still sitting at the BASIC prompt. Long afterwards, in chat
    mode, every character of the probe must still be displayed.
    """
    bbc = commstar_after_stale_burst
    transmitted(bbc)

    _receive(bbc, PROBE)

    shown = _displayed(bbc)
    assert PROBE.decode() in shown, (
        f"expected {PROBE.decode()!r} on screen, got {shown!r}; "
        "a leading character lost here is the #59 regression"
    )

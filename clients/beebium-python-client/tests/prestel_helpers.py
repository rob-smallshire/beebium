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

"""Shared Commstar/Prestel constants and navigation helpers.

A properly named helper module (like tube_test_helpers.py / firetrack.py), NOT
conftest.py: the two Commstar test modules and the Commstar fixtures in
conftest.py all import these from here. Importing a conftest.py as a plain
module (``from conftest import ...``) is fragile -- pytest can bind the bare
name ``conftest`` to a different directory's conftest.py when several are
collected together -- so shared symbols live in a uniquely named module instead.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.ext.peripheral.rpc_serial import RpcSerial

COMMSTAR_ROM_FILENAME = "commstar_1_40_SN882A.rom"

# Prestel transmits at 75 baud: a single 10-bit frame takes about 130ms of
# emulated time, so allow generous settling before reading the wire.
TRANSMIT_SETTLE_SECONDS = 2.0

# Commstar samples the keyboard *after* taking the character, so a key must
# stay down long enough to be seen by that scan. The MOS scans on the 100Hz
# interrupt; 150ms is several scans' worth.
KEY_HOLD_SECONDS = 0.15

VIEWDATA_HASH = b"_"  # 0x5F -- '#' in the teletext repertoire
CARRIAGE_RETURN = b"\r"


def screen(bbc: Beebium) -> str:
    """The whole screen as one string, for substring assertions."""
    text = bbc.video.screen_text().text
    return "\n".join(text) if isinstance(text, list) else text


def wait_for_screen(bbc: Beebium, needle: str, timeout: float = 10.0) -> None:
    """Poll the screen until `needle` appears, rather than sleeping blind."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if needle in screen(bbc):
            return
        time.sleep(0.1)
    pytest.fail(f"{needle!r} did not appear on screen. Screen was:\n{screen(bbc)}")


def enter_prestel_chat(bbc: Beebium) -> None:
    """Start Commstar, select Prestel emulation, and enter chat mode."""
    wait_for_screen(bbc, "BASIC")
    bbc.keyboard.type("*COMMSTAR\r")
    wait_for_screen(bbc, "Select ?")

    # The Comms/Prestel toggle. Commstar's own menu renders the key as '_',
    # since 0x5F is '#' in the teletext repertoire it displays in.
    bbc.keyboard.type("#")
    wait_for_screen(bbc, "Prestel")

    bbc.keyboard.type("C")
    time.sleep(1.0)  # chat mode clears the screen; nothing specific to await


def transmitted(bbc: Beebium) -> bytes:
    """Bytes the BBC has put on the wire since the last read."""
    time.sleep(TRANSMIT_SETTLE_SECONDS)
    return bytes(bbc.extensions[RpcSerial].receive())

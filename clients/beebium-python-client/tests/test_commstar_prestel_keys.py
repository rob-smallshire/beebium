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

"""Period comms software driving the serial port: Pace Commstar in viewdata mode.

Commstar's Prestel emulation transmits the viewdata "proceed to next frame"
character (0x5F, which is `#` in the teletext repertoire) when RETURN is
pressed, and a genuine carriage return when CTRL-M is pressed. Both keys
produce MOS character code 13, so Commstar tells them apart by scanning the
physical keyboard: OSBYTE 122 (scan from key 16, which skips SHIFT and CTRL)
followed by a comparison against RETURN's internal key number 0x49.

That makes these tests an end-to-end exercise of the keyboard matrix, the MOS
keyboard scan, the Serial ULA at viewdata's 7E1 word format, and a serial
device extension -- with real period software as the oracle. The expected bytes
were confirmed against Commstar 1.40 running on real hardware.
"""

import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
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


@pytest.fixture(scope="module")
def commstar_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the Commstar comms ROM."""
    path = beebium_roms_dirpath / COMMSTAR_ROM_FILENAME
    if not path.exists():
        pytest.skip(f"Commstar ROM not found: {path}")
    return path


@pytest.fixture
def commstar_prestel_bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    commstar_rom_filepath: Path,
):
    """A BBC running Commstar in Prestel chat mode, its serial port on rpc-serial."""
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
            _enter_prestel_chat(bbc)
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _screen(bbc: Beebium) -> str:
    """The whole screen as one string, for substring assertions."""
    text = bbc.video.screen_text().text
    return "\n".join(text) if isinstance(text, list) else text


def _wait_for_screen(bbc: Beebium, needle: str, timeout: float = 10.0) -> None:
    """Poll the screen until `needle` appears, rather than sleeping blind."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if needle in _screen(bbc):
            return
        time.sleep(0.1)
    pytest.fail(f"{needle!r} did not appear on screen. Screen was:\n{_screen(bbc)}")


def _enter_prestel_chat(bbc: Beebium) -> None:
    """Start Commstar, select Prestel emulation, and enter chat mode."""
    _wait_for_screen(bbc, "BASIC")
    bbc.keyboard.type("*COMMSTAR\r")
    _wait_for_screen(bbc, "Select ?")

    # The Comms/Prestel toggle. Commstar's own menu renders the key as '_',
    # since 0x5F is '#' in the teletext repertoire it displays in.
    bbc.keyboard.type("#")
    _wait_for_screen(bbc, "Prestel")

    bbc.keyboard.type("C")
    time.sleep(1.0)  # chat mode clears the screen; nothing specific to await


def _transmitted(bbc: Beebium) -> bytes:
    """Bytes the BBC has put on the wire since the last read."""
    time.sleep(TRANSMIT_SETTLE_SECONDS)
    return bytes(bbc.extensions[RpcSerial].receive())


def test_return_transmits_the_viewdata_hash(commstar_prestel_bbc):
    """RETURN in Prestel mode sends 0x5F -- viewdata's 'proceed to next frame'."""
    bbc = commstar_prestel_bbc
    _transmitted(bbc)  # discard anything from entering chat mode

    bbc.keyboard.press_return()

    assert _transmitted(bbc) == VIEWDATA_HASH


def test_ctrl_m_transmits_a_carriage_return(commstar_prestel_bbc):
    """CTRL-M in Prestel mode sends a real CR, despite producing the same code 13.

    This is what lets a Hayes AT command be terminated without leaving viewdata
    emulation. It works only because the emulated keyboard matrix still reports
    M (not RETURN) held when Commstar's OSBYTE 122 scan runs.
    """
    bbc = commstar_prestel_bbc
    _transmitted(bbc)

    keyboard = bbc.keyboard
    keyboard.ctrl_down()
    time.sleep(KEY_HOLD_SECONDS)
    keyboard.key_down("m")
    time.sleep(KEY_HOLD_SECONDS)
    keyboard.key_up("m")
    keyboard.ctrl_up()

    assert _transmitted(bbc) == CARRIAGE_RETURN

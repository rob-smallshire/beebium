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

They drive the keyboard matrix directly, so they cover the emulator and nothing
above it. A front-end can still resolve CTRL-M to the wrong physical key and
these tests stay green: the macOS client did exactly that until it was fixed to
stop resolving keys through CTRL (see rule R9 in docs/frontend-modifier-keys.md).
"""

import time

from prestel_helpers import (
    CARRIAGE_RETURN,
    KEY_HOLD_SECONDS,
    VIEWDATA_HASH,
    transmitted,
)


def test_return_transmits_the_viewdata_hash(commstar_prestel_bbc):
    """RETURN in Prestel mode sends 0x5F -- viewdata's 'proceed to next frame'."""
    bbc = commstar_prestel_bbc
    transmitted(bbc)  # discard anything from entering chat mode

    bbc.keyboard.press_return()

    assert transmitted(bbc) == VIEWDATA_HASH


def test_ctrl_m_transmits_a_carriage_return(commstar_prestel_bbc):
    """CTRL-M in Prestel mode sends a real CR, despite producing the same code 13.

    This is what lets a Hayes AT command be terminated without leaving viewdata
    emulation. It works only because the emulated keyboard matrix still reports
    M (not RETURN) held when Commstar's OSBYTE 122 scan runs.
    """
    bbc = commstar_prestel_bbc
    transmitted(bbc)

    keyboard = bbc.keyboard
    keyboard.ctrl_down()
    time.sleep(KEY_HOLD_SECONDS)
    keyboard.key_down("m")
    time.sleep(KEY_HOLD_SECONDS)
    keyboard.key_up("m")
    keyboard.ctrl_up()

    assert transmitted(bbc) == CARRIAGE_RETURN

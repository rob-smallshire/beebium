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

"""README snippet: the cassette deck (uses the pytest `launch_bbc` factory)."""

import struct


def _tone_uef(seconds: int) -> bytes:
    """A UEF of nothing but carrier."""
    cycles = seconds * 2400
    return b"UEF File!\x00\x0a\x00" + struct.pack("<HIH", 0x0110, 2, cycles)


def test_cassette(launch_bbc, tmp_path):
    (tmp_path / "game.uef").write_bytes(_tone_uef(10))
    bbc = launch_bbc()
    bbc.expect("BASIC")
    # readme:begin
    # Insert a tape: the deck is left stopped at the start.
    tape = bbc.cassette.insert(tmp_path / "game.uef")
    print(tape.name, tape.format, tape.duration_seconds)  # -> game.uef UEF 10.0

    # Play, and the tape moves whenever the Beeb's motor relay is closed,
    # which the filing system does when it wants the tape.
    bbc.cassette.play()
    status = bbc.cassette.status()
    print(status.transport, status.motor_on, status.position_seconds)

    bbc.cassette.seek(5.0)       # seek(0) is back to the beginning
    bbc.cassette.fast_forward()  # rewind() and fast_forward() wind at 30x
    bbc.cassette.stop()
    bbc.cassette.eject()

    # To load a program from tape as a user would -- *TAPE, CHAIN"" and play:
    #   bbc.boot_tape("game.uef")                       # or command="*RUN"
    # readme:end
    assert bbc.cassette.status().tape is None

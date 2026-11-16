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

"""README snippet: SAVE to a blank tape and CHAIN it back (uses `launch_bbc`)."""

import contextlib


def test_cassette_recording(launch_bbc, tmp_path):
    bbc = launch_bbc()
    bbc.expect("BASIC")
    # readme:begin
    # A blank C15 on the server's host, in the deck.
    tape = bbc.cassette.create_tape("Blank C15 (15 minutes)", tmp_path / "hello")
    bbc.cassette.insert(tape)

    bbc.keyboard.type('10 PRINT "HELLO FROM TAPE"\r*TAPE\rSAVE "HELLO"\r')
    bbc.expect("RECORD then RETURN")
    bbc.cassette.record()
    bbc.keyboard.press_return()

    # The SAVE is over when the motor relay opens again.
    with contextlib.closing(bbc.cassette.events()) as events:
        for status in events:
            if not status.motor_on and status.position_seconds > 0:
                break

    bbc.cassette.seek(0)    # back to the beginning; the recording is written to the file
    bbc.cassette.play()
    bbc.keyboard.type('NEW\rCHAIN "HELLO"\r')
    bbc.expect("HELLO FROM TAPE", timeout=60)
    # readme:end

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

"""README snippet: launch a server with no arguments (the flagship example)."""


def test_launch():
    # readme:begin
    from beebium.client import Beebium

    # With the beebium-server package installed, launch() needs no arguments:
    # the emulator binary and its ROMs come from that wheel, and the server is
    # stopped again when the block exits.
    with Beebium.launch() as bbc:
        bbc.expect("BASIC")            # wait for the boot banner / BASIC prompt
        bbc.keyboard.type("PRINT 2+2")
        bbc.keyboard.press_return()
        print(bbc.expect("4"))         # -> 4
    # readme:end

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

"""README snippet: connect to a server that is already running."""

import beebium.client as _beebium


def test_connect():
    # Scaffolding: start a server for the example to connect to. Not rendered.
    # Aliased so it never collides with the region's own `Beebium` import.
    with _beebium.Beebium.launch(port=48875):
        # readme:begin
        from beebium.client import Beebium

        # Connect to a server that is already running. The target defaults to
        # localhost on port 48875 (0xBEEB); pass "host:port" to reach another.
        with Beebium.connect() as bbc:
            bbc.debugger.stop()
            print(f"PC = 0x{bbc.cpu.pc:04X}")
        # readme:end

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

"""README snippet: peek and poke memory (uses the pytest `stopped_bbc` fixture)."""


def test_memory(stopped_bbc):
    bbc = stopped_bbc
    # readme:begin
    # Side-effect-free peek -- safe even for I/O registers.
    via_flags = bbc.memory.address.peek[0xFE4D]        # System VIA IFR
    zero_page = bbc.memory.address.peek[0x0000:0x0100]

    # Follow a 6502 vector (16-bit little-endian).
    wrchv = bbc.memory.address.peek.word(0x020E)       # the OSWRCH vector

    # Bus access reads and writes through the memory bus, like real hardware.
    bbc.memory.address.bus[0x1900] = 0x42
    value = bbc.memory.address.bus[0x1900]
    # readme:end
    assert value == 0x42
    assert len(zero_page) == 0x100
    _ = (via_flags, wrchv)

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

"""Package interaction smoke test.

Run against a server installed from a built package (.deb or extracted .tar.gz):
the server binary is on PATH (or under /opt/beebium) and the ROMs are shipped in
the bundle. Uses the `bbc` fixture from the beebium pytest plugin, which launches
the installed server and connects the Python client to it -- so a passing run
proves the whole user path works end to end: install -> launch -> gRPC -> ROMs ->
a real BASIC interaction.

In CI the ROM directory is pointed at the bundle via BEEBIUM_ROM_DIR
(/opt/beebium/share/beebium/roms) so the fixture can find the MOS and BASIC ROMs.
"""

from __future__ import annotations

from beebium.client import Beebium


def test_boots_to_basic_and_evaluates(bbc: Beebium) -> None:
    # Reaching this point already means the installed server launched and the
    # client connected (the fixture would have failed otherwise). Confirm it
    # boots to the BASIC prompt and evaluates an expression.
    bbc.basic.wait_for_prompt(timeout=15)
    bbc.keyboard.type("PRINT 2+3\r")
    assert bbc.basic.wait_for_text("5", timeout=15)

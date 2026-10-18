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

"""The MODE 7 screen helpers read the screen as displayed, in shadow modes too.

In MODE 135 the screen is not what the CPU sees at &7C00: the B+ displays its
shadow RAM, and the Integra-B displays main RAM while the CPU sees its shadow
RAM there. The helpers follow the display on every model, where a peek at
&7C00 would find the text only on a Model B.
"""

from __future__ import annotations

import pytest

from beebium.client.screen import dump_screen, find, read_mode7_screen, screen_contains

# Printed by a command whose own text does not contain it, so finding it on
# the screen means the command ran rather than that it was echoed. BASIC then
# loops, so the machine stops in BASIC: in the OS's idle loop the Integra-B's
# IBOS selects main memory, and the CPU would see the screen there.
MARKER = "SHADOWED SCREEN"
COMMAND = 'MODE 135:PRINT "SHADOWED"+" SCREEN":REPEAT UNTIL FALSE\r'


@pytest.mark.parametrize(
    ("variant", "shadowed"),
    [
        ("model-b", False),
        ("model-b-plus", True),
        ("model-b-plus-128k", True),
        ("model-b-integra-b", True),
    ],
)
def test_the_helpers_read_the_displayed_screen(launch_bbc, variant: str, shadowed: bool) -> None:
    bbc = launch_bbc(variant=variant)
    assert bbc.run_until_or_timeout(lambda: screen_contains(bbc, ">"), emulated_seconds=10.0), (
        dump_screen(bbc))

    bbc.keyboard.type(COMMAND)
    assert bbc.run_until_or_timeout(lambda: screen_contains(bbc, MARKER), emulated_seconds=10.0), (
        dump_screen(bbc))

    assert any(MARKER in row for row in read_mode7_screen(bbc))
    assert find(bbc, MARKER) is not None
    cpu_view = bbc.memory.address.peek.read(0x7C00, 0x400)
    assert (MARKER.encode() in cpu_view) is not shadowed, (
        "the CPU's view of &7C00 should hold the screen only when the mode is not shadowed")

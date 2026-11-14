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

"""The analogue port as an attachment point, seen through the client (#98).

Test 12 of docs/discussion/analogue-port-seam-specification.md: the machine's
own attachment points include the analogue port, named for a user, taking at
most one device.
"""

from __future__ import annotations

from beebium.client import Beebium


def test_a_model_b_offers_the_analogue_port(bbc: Beebium) -> None:
    points = {point.id: point for point in bbc.extensions.attachment_points}
    assert "analogue-port" in points
    port = points["analogue-port"]
    assert port.display_name == "Analogue Port"
    assert port.min_occupancy == 0
    assert port.max_occupancy == 1
    assert port.provided_by == ()

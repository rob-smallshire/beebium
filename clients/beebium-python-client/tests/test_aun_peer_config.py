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

"""AUN peer configuration and transport activation against a real server.

Covers issue #54 (EconetService.Enable with an aun_port must bind the configured
transport, so AUN status reflects it) and issue #55 (the peer table is editable
before the backend is up; entries survive the backend being brought up).
"""

from __future__ import annotations

from pathlib import Path

from beebium.client import Beebium
from beebium.ext.econet.aun import Aun


def test_enable_with_port_binds_the_aun_transport(
    mos_filepath: Path, beebium_server_filepath: Path | None
) -> None:
    # #54: with the AUN transport configured but no station at launch, the
    # backend is inactive; Enable(aun_port=...) must bring it up through the
    # transport so AUN status shows the bound port, not local_port=0.
    with Beebium.launch(
        server=beebium_server_filepath,
        mos_filepath=mos_filepath,
        extra_args=["--aun", "net=1"],
    ) as bbc:
        assert bbc.transport[Aun].status.local_port == 0
        assert bbc.transport[Aun].status.connected is False

        bbc.econet.enable(station_id=254, aun_port=32768)

        status = bbc.transport[Aun].status
        assert status.local_port == 32768
        assert status.connected is True

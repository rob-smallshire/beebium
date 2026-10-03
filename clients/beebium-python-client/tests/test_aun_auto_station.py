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

"""The automatic-station preset against a real server (issue #67).

The bundled ``model-b-disc-aun-auto`` preset carries ``"station": "auto"``: the
server chooses a free Econet station number at launch, browsing ``_aun._udp``,
so several clients can be started without renumbering. This checks the preset
launches and comes up with a station in the default auto range. It skips when no
server is available (the ``server_installation`` fixture handles that)."""

from __future__ import annotations

from pathlib import Path

from beebium.client import Beebium
from beebium.client.installation import ServerInstallation


def test_auto_station_preset_comes_up_in_range(
    mos_filepath: Path, server_installation: ServerInstallation
) -> None:
    # The auto browse runs during server start-up, so allow a little longer for
    # the gRPC endpoint to become ready than the default.
    with Beebium.launch(
        server=server_installation,
        mos_filepath=mos_filepath,
        preset="model-b-disc-aun-auto",
        startup_timeout=30.0,
    ) as bbc:
        status = bbc.econet.status
        assert status.enabled
        # A number was chosen in the preset's default range (80-253), not left
        # at 0 and not out of range. The exact value depends on what else is on
        # the net, so only the range is asserted.
        assert 80 <= status.station_id <= 253

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
from beebium.ext.econet.aun import Aun, PeerSource


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


def test_add_peer_before_enable_survives_and_is_listed(
    mos_filepath: Path, beebium_server_filepath: Path | None
) -> None:
    # #55: the peer table lives in the transport, not the backend, so AddPeer
    # works before the socket is up and the entry survives Enable bringing it
    # up. Previously this failed with "AUN backend is not active".
    with Beebium.launch(
        server=beebium_server_filepath,
        mos_filepath=mos_filepath,
        extra_args=["--aun", "net=1"],
    ) as bbc:
        aun = bbc.transport[Aun]

        # No backend yet, but AddPeer still takes the entry.
        aun.add_peer(net=1, stn=254, ip_address="127.0.0.1", port=40001)
        peers = aun.peers
        assert len(peers) == 1
        assert peers[0].stn == 254
        assert peers[0].port == 40001
        assert peers[0].source == PeerSource.API

        # Bring the transport up; the entry survives and is now routable.
        bbc.econet.enable(station_id=200, aun_port=32768)
        assert bbc.transport[Aun].status.local_port == 32768

        peers = aun.peers
        assert len(peers) == 1
        assert peers[0].stn == 254
        assert peers[0].source == PeerSource.API


def test_api_peer_survives_disable_and_reenable(
    mos_filepath: Path, beebium_server_filepath: Path | None
) -> None:
    # #55: DisableEconet frees the backend. The peer table lives in the
    # transport, so the Api peer survives, peer edits after Disable are safe
    # (no use-after-free on the freed backend), and the peer is routed again
    # after a re-Enable.
    with Beebium.launch(
        server=beebium_server_filepath,
        mos_filepath=mos_filepath,
        extra_args=["--aun", "net=1"],
    ) as bbc:
        aun = bbc.transport[Aun]

        bbc.econet.enable(station_id=200, aun_port=32768)
        aun.add_peer(net=1, stn=100, ip_address="127.0.0.1", port=40001)
        assert aun.status.local_port == 32768
        assert len(aun.peers) == 1

        bbc.econet.disable()
        # No backend now: status reports no link, but peer edits stay safe and
        # the Api entry is retained in the transport's peer set.
        assert aun.status.connected is False
        assert aun.status.local_port == 0
        aun.add_peer(net=1, stn=101, ip_address="127.0.0.1", port=40002)
        peers = {p.stn for p in aun.peers}
        assert {100, 101} <= peers

        # Re-enable: the Api peers are applied to the fresh backend.
        bbc.econet.enable(station_id=200, aun_port=32768)
        assert bbc.transport[Aun].status.local_port == 32768
        peers = {p.stn for p in aun.peers}
        assert {100, 101} <= peers
        assert all(p.source == PeerSource.API for p in aun.peers)

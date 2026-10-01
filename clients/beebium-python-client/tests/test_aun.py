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

"""Unit tests for the Aun client wrapper around AunService.

The Aun wrapper tunnels its messages over the core's ExtensionRpc channel, so
the tests drive it through a fake ExtensionChannel whose ``invoke`` returns
serialized real protobuf responses and records the serialized requests (which
the tests decode to assert on the fields the wrapper sent).
"""

from __future__ import annotations

import pytest

from beebium.client.exceptions import EconetError
from beebium.ext.econet.aun import Aun, AunStatus, PeerInfo, PeerSource
from beebium.ext.econet.aun._proto import aun_pb2


class FakeChannel:
    """An ExtensionChannel stand-in for AunService methods."""

    def __init__(self):
        self._responses = {
            "GetStatus": aun_pb2.AunGetStatusResponse(connected=True, local_port=32768, peer_count=0),
            "SetConnected": aun_pb2.AunSetConnectedResponse(success=True),
            "AddPeer": aun_pb2.AunAddPeerResponse(success=True),
            "RemovePeer": aun_pb2.AunRemovePeerResponse(success=True),
            "ListPeers": aun_pb2.AunListPeersResponse(),
        }
        self.calls: list[tuple[str, str, bytes]] = []

    def set_response(self, method, response) -> None:
        self._responses[method] = response

    def invoke(self, service, method, payload, *, extension_id=""):
        self.calls.append((service, method, payload))
        return self._responses[method].SerializeToString()

    def request(self, method, request_type):
        """Decode the request the wrapper serialized for `method`."""
        for service, called_method, payload in self.calls:
            if called_method == method:
                assert service == "AunService"
                req = request_type()
                req.ParseFromString(payload)
                return req
        raise AssertionError(f"{method} was not invoked")


@pytest.fixture
def channel():
    return FakeChannel()


@pytest.fixture
def aun(channel):
    return Aun("aun", channel)


class TestStatus:
    def test_status_returns_dataclass(self, channel, aun):
        channel.set_response(
            "GetStatus",
            aun_pb2.AunGetStatusResponse(connected=True, local_port=32768, peer_count=2),
        )
        status = aun.status
        assert isinstance(status, AunStatus)
        assert status.connected is True
        assert status.local_port == 32768
        assert status.peer_count == 2


class TestSetConnected:
    def test_set_connected_true(self, channel, aun):
        aun.set_connected(True)
        request = channel.request("SetConnected", aun_pb2.AunSetConnectedRequest)
        assert request.connected is True

    def test_set_connected_false(self, channel, aun):
        aun.set_connected(False)
        request = channel.request("SetConnected", aun_pb2.AunSetConnectedRequest)
        assert request.connected is False

    def test_set_connected_failure_raises(self, channel, aun):
        channel.set_response(
            "SetConnected",
            aun_pb2.AunSetConnectedResponse(success=False, error="AUN backend is not active"),
        )
        with pytest.raises(EconetError, match="not active"):
            aun.set_connected(True)


class TestAddPeer:
    def test_add_peer(self, channel, aun):
        aun.add_peer(net=1, stn=254, ip_address="192.168.1.100", port=32768)
        request = channel.request("AddPeer", aun_pb2.AunAddPeerRequest)
        assert request.net == 1
        assert request.stn == 254
        assert request.ip_address == "192.168.1.100"
        assert request.port == 32768

    def test_add_peer_default_port(self, channel, aun):
        aun.add_peer(net=0, stn=1, ip_address="10.0.0.1")
        request = channel.request("AddPeer", aun_pb2.AunAddPeerRequest)
        assert request.port == 0

    def test_add_peer_failure_raises(self, channel, aun):
        channel.set_response(
            "AddPeer",
            aun_pb2.AunAddPeerResponse(success=False, error="invalid ip_address"),
        )
        with pytest.raises(EconetError, match="invalid ip_address"):
            aun.add_peer(net=0, stn=1, ip_address="not.an.ip")


class TestRemovePeer:
    def test_remove_peer(self, channel, aun):
        aun.remove_peer(net=1, stn=254)
        request = channel.request("RemovePeer", aun_pb2.AunRemovePeerRequest)
        assert request.net == 1
        assert request.stn == 254

    def test_remove_peer_failure_raises(self, channel, aun):
        channel.set_response(
            "RemovePeer",
            aun_pb2.AunRemovePeerResponse(success=False, error="AUN backend is not active"),
        )
        with pytest.raises(EconetError, match="not active"):
            aun.remove_peer(net=0, stn=99)


class TestPeers:
    def test_peers_empty(self, aun):
        assert aun.peers == []

    def test_peers_populated(self, channel, aun):
        channel.set_response(
            "ListPeers",
            aun_pb2.AunListPeersResponse(
                peers=[
                    aun_pb2.AunPeer(
                        net=0,
                        stn=1,
                        ip_address="192.168.1.1",
                        port=32768,
                        source=aun_pb2.AUN_PEER_SOURCE_API,
                    ),
                    aun_pb2.AunPeer(
                        net=1,
                        stn=254,
                        ip_address="10.0.0.100",
                        port=9999,
                        source=aun_pb2.AUN_PEER_SOURCE_DISCOVERED,
                    ),
                ]
            ),
        )
        peers = aun.peers
        assert len(peers) == 2
        assert isinstance(peers[0], PeerInfo)
        assert peers[0].net == 0
        assert peers[0].ip_address == "192.168.1.1"
        assert peers[0].source == PeerSource.API
        assert peers[1].port == 9999
        assert peers[1].source == PeerSource.DISCOVERED

    def test_peers_reports_launch_and_map_file_sources(self, channel, aun):
        # The remaining two provenances round-trip as themselves.
        channel.set_response(
            "ListPeers",
            aun_pb2.AunListPeersResponse(
                peers=[
                    aun_pb2.AunPeer(
                        net=0,
                        stn=1,
                        ip_address="192.168.1.1",
                        port=32768,
                        source=aun_pb2.AUN_PEER_SOURCE_LAUNCH,
                    ),
                    aun_pb2.AunPeer(
                        net=0,
                        stn=2,
                        ip_address="192.168.1.2",
                        port=32768,
                        source=aun_pb2.AUN_PEER_SOURCE_MAP_FILE,
                    ),
                ]
            ),
        )
        peers = aun.peers
        assert peers[0].source == PeerSource.LAUNCH
        assert peers[1].source == PeerSource.MAP_FILE

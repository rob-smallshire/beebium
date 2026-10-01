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

"""AUN (Acorn Universal Networking) transport-specific operations.

These RPCs are surfaced by AunService when AUN is the active Econet
transport on the server. Use bbc.transport.active to confirm AUN is
the active transport before calling these methods; otherwise the RPC
returns an error indicating "AUN backend is not active".
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

from beebium.client.exceptions import EconetError
from beebium.client.extension import EconetTransportAdapter
from beebium.ext.econet.aun._proto import aun_pb2

# The logical service name the AUN extension's dispatcher registers
# (matches AunDispatcher::service_name() in the extension).
_SERVICE = "AunService"


class PeerSource(IntEnum):
    """Where an AUN peer entry came from.

    Resolved by precedence, highest first: ``API``, ``LAUNCH``,
    ``MAP_FILE``, ``DISCOVERED``. The three operator sources (a runtime
    :meth:`Aun.add_peer`, the CLI ``--aun map=`` / preset for this launch,
    and the per-user map file) all take precedence over discovered peers
    in the routing table.
    """

    UNSPECIFIED = aun_pb2.AUN_PEER_SOURCE_UNSPECIFIED
    LAUNCH = aun_pb2.AUN_PEER_SOURCE_LAUNCH
    API = aun_pb2.AUN_PEER_SOURCE_API
    MAP_FILE = aun_pb2.AUN_PEER_SOURCE_MAP_FILE
    DISCOVERED = aun_pb2.AUN_PEER_SOURCE_DISCOVERED


@dataclass(frozen=True)
class AunStatus:
    """AUN-specific transport status."""

    connected: bool
    local_port: int
    peer_count: int


@dataclass(frozen=True)
class PeerInfo:
    """An AUN peer mapping."""

    net: int
    stn: int
    ip_address: str
    port: int
    # The source this resolved entry won from: LAUNCH (--aun map= / preset),
    # API (a runtime add_peer), MAP_FILE (the per-user map file), or
    # DISCOVERED (the AUN extension's mDNS subscriber).
    source: PeerSource = PeerSource.LAUNCH


class Aun(EconetTransportAdapter):
    """AUN-specific RPCs (peer table, cable plug, port status).

    Available on the server's gRPC surface only when AUN is the active
    Econet transport. Check ``bbc.transport.active`` first if your code
    might run against a server configured for Piconet or no transport.

    Usage:
        aun = bbc.extensions[Aun]        # or Aun.attach(bbc)
        aun.add_peer(net=0, stn=254, ip_address="192.168.1.10")
        print(aun.status)
    """

    EXTENSION_NAME = "aun"

    def _invoke(self, method: str, request, response):
        """Tunnel `request` to the AunService dispatcher and parse the reply.

        The AUN messages travel over the core's ExtensionRpc channel; the AUN
        extension no longer hosts its own gRPC service. The public API here is
        unchanged.
        """
        reply = self._invoke_bytes(_SERVICE, method, request.SerializeToString())
        response.ParseFromString(reply)
        return response

    @property
    def status(self) -> AunStatus:
        """Read the AUN backend status."""
        request = aun_pb2.AunGetStatusRequest()
        response = self._invoke("GetStatus", request, aun_pb2.AunGetStatusResponse())
        return AunStatus(
            connected=response.connected,
            local_port=response.local_port,
            peer_count=response.peer_count,
        )

    @property
    def peers(self) -> list[PeerInfo]:
        """Enumerate all configured AUN peers."""
        request = aun_pb2.AunListPeersRequest()
        response = self._invoke("ListPeers", request, aun_pb2.AunListPeersResponse())
        return [
            PeerInfo(
                net=p.net,
                stn=p.stn,
                ip_address=p.ip_address,
                port=p.port,
                source=PeerSource(p.source),
            )
            for p in response.peers
        ]

    def set_connected(self, connected: bool) -> None:
        """Plug or unplug the simulated network cable.

        While disconnected the ADLC sees DCD high (no carrier).

        Raises:
            EconetError: If the AUN backend is not active or the call fails.
        """
        request = aun_pb2.AunSetConnectedRequest(connected=connected)
        response = self._invoke("SetConnected", request, aun_pb2.AunSetConnectedResponse())
        if not response.success:
            raise EconetError(response.error)

    def add_peer(
        self,
        net: int,
        stn: int,
        ip_address: str,
        port: int = 0,
    ) -> None:
        """Add an Econet address to UDP endpoint peer mapping.

        Args:
            net: Econet network number (0-127).
            stn: Econet station number (1-254).
            ip_address: Dotted-quad IP address.
            port: UDP port (0 = use AUN default 32768).

        Raises:
            EconetError: If the call fails.
        """
        request = aun_pb2.AunAddPeerRequest(
            net=net,
            stn=stn,
            ip_address=ip_address,
            port=port,
        )
        response = self._invoke("AddPeer", request, aun_pb2.AunAddPeerResponse())
        if not response.success:
            raise EconetError(response.error)

    def remove_peer(self, net: int, stn: int) -> None:
        """Remove a peer mapping by Econet address.

        Raises:
            EconetError: If the call fails.
        """
        request = aun_pb2.AunRemovePeerRequest(net=net, stn=stn)
        response = self._invoke("RemovePeer", request, aun_pb2.AunRemovePeerResponse())
        if not response.success:
            raise EconetError(response.error)

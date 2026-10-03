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

These RPCs are served by the AUN transport's AunService, tunnelled over the
core's ExtensionRpc channel. They are reachable whenever the server has the
AUN transport loaded (``--aun`` or a preset's ``econet.transport``), whether
or not its socket is up yet. Reach the adapter with ``bbc.transport[Aun]``
(or ``Aun.attach(bbc)``), which raises ``ExtensionNotLoadedError`` when no
AUN transport is loaded; ``bbc.transport.active`` reports which transport,
if any, is loaded.
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

    Each (net, stn) resolves to one winner by precedence, highest first:
    ``API``, ``LAUNCH``, ``MAP_FILE``, ``DISCOVERED``, ``SUBNET``. The three
    operator sources (a runtime :meth:`Aun.add_peer`, the CLI ``--aun map=`` /
    preset for this launch, and the map file) take precedence over
    discovered peers, which in turn beat a ``SUBNET`` entry materialised
    from a subnet rule (the map file's ``subnets`` or ``--aun subnet=``).
    Removing the winner falls back to the next source still present.
    """

    UNSPECIFIED = aun_pb2.AUN_PEER_SOURCE_UNSPECIFIED
    LAUNCH = aun_pb2.AUN_PEER_SOURCE_LAUNCH
    API = aun_pb2.AUN_PEER_SOURCE_API
    MAP_FILE = aun_pb2.AUN_PEER_SOURCE_MAP_FILE
    DISCOVERED = aun_pb2.AUN_PEER_SOURCE_DISCOVERED
    SUBNET = aun_pb2.AUN_PEER_SOURCE_SUBNET


@dataclass(frozen=True)
class AunStatus:
    """AUN-specific transport status."""

    # True if the AUN socket is bound and the cable is plugged in; False
    # while there is no socket.
    connected: bool
    # The bound UDP port, 0 while there is no socket.
    local_port: int
    # The number of (net, stn) entries in the resolved routing table (what
    # Aun.peers returns), counted even before the socket is up.
    peer_count: int
    # The map file's path on the server's host (--aun map-file=, else
    # BEEBIUM_AUN_MAP_FILEPATH, else the per-user aun-map.json; empty when
    # disabled with map-file=none), its entry count from the last load, and
    # the last load error (empty on success or an absent file).
    map_file_path: str = ""
    map_file_entry_count: int = 0
    map_file_error: str = ""
    # The mDNS discovery mode set by --aun discovery=: "on", "announce",
    # "browse" or "off".
    discovery_mode: str = "on"


@dataclass(frozen=True)
class PeerInfo:
    """An AUN peer mapping."""

    net: int
    stn: int
    ip_address: str
    port: int
    # The source this resolved entry won from: API (a runtime add_peer),
    # LAUNCH (--aun map= / preset), MAP_FILE (the map file), DISCOVERED (the
    # AUN extension's mDNS subscriber) or SUBNET (materialised from a subnet
    # rule).
    source: PeerSource = PeerSource.LAUNCH


@dataclass(frozen=True)
class MapPeer:
    """A peers[] entry in the map file, with its host-resolution state."""

    net: int
    stn: int
    host: str  # as written (IPv4 literal or DNS name)
    port: int
    label: str
    resolved: bool
    resolved_ip: str  # dotted-quad when resolved, else empty


@dataclass(frozen=True)
class MapSubnet:
    """A subnets[] entry in the map file."""

    net: int
    subnet: str
    label: str


@dataclass(frozen=True)
class MapListing:
    """The map file's entries (distinct from the live routing table)."""

    peers: list[MapPeer]
    subnets: list[MapSubnet]


class Aun(EconetTransportAdapter):
    """AUN-specific RPCs (peer table, map file, cable plug, status).

    Available whenever the server has the AUN transport loaded. Check
    ``bbc.transport.active`` first (or use ``bbc.transport.get(Aun)``) if
    your code might run against a server configured for Piconet or no
    transport.

    Usage:
        aun = bbc.transport[Aun]         # or Aun.attach(bbc)
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
        """Read the AUN transport status (link, port, peers, map file, discovery mode)."""
        request = aun_pb2.AunGetStatusRequest()
        response = self._invoke("GetStatus", request, aun_pb2.AunGetStatusResponse())
        return AunStatus(
            connected=response.connected,
            local_port=response.local_port,
            peer_count=response.peer_count,
            map_file_path=response.map_file_path,
            map_file_entry_count=response.map_file_entry_count,
            map_file_error=response.map_file_error,
            discovery_mode=response.discovery_mode or "on",
        )

    @property
    def peers(self) -> list[PeerInfo]:
        """The resolved routing table: one entry per (net, stn).

        Each entry is the winning source's endpoint, from any source (API,
        launch, map file, mDNS or a subnet rule); see :class:`PeerSource`.
        Map-file peers whose host did not resolve are not here; see
        :meth:`list_map`.
        """
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

        While disconnected the ADLC sees DCD high (no carrier). Before the
        AUN socket is up the state is remembered and applied when it comes
        up.

        Raises:
            EconetError: If the server reports the call failed.
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
        """Add or replace this client's (``API``) peer mapping for an Econet address.

        ``API`` entries take precedence over every other source for that
        (net, stn). They work before the AUN socket is up (applied when it
        comes up) and are not written to the map file; use
        :meth:`add_map_peer` for a peer every instance should share.

        Args:
            net: Econet network number (0-255).
            stn: Econet station number (1-254).
            ip_address: Dotted-quad IPv4 address (a DNS name is rejected).
            port: UDP port (0 = use AUN default 32768).

        Raises:
            EconetError: On a validation error (net, stn or ip_address).
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
        """Remove the ``API`` entry :meth:`add_peer` made for an Econet address.

        Entries from other sources are untouched, so a station also named by
        the launch config, the map file or mDNS falls back to that entry.
        Removing an address with no ``API`` entry is not an error.

        Raises:
            EconetError: If the server reports the call failed.
        """
        request = aun_pb2.AunRemovePeerRequest(net=net, stn=stn)
        response = self._invoke("RemovePeer", request, aun_pb2.AunRemovePeerResponse())
        if not response.success:
            raise EconetError(response.error)

    def reload_map(self) -> None:
        """Re-read the per-user ``aun-map.json`` on the server now.

        Replaces only the map file's contributions (``MAP_FILE`` peers, the
        map file's subnet rules, and the ``SUBNET`` peers materialised from
        subnet rules); ``API``, ``LAUNCH`` and ``DISCOVERED`` entries and
        ``--aun subnet=`` rules are untouched. A modification is picked up
        automatically by the mtime poll while discovery browses
        (``discovery=on`` or ``browse``); this forces it. With the map file
        disabled (``map-file=none``) it does nothing.

        Raises:
            EconetError: If the file was present but could not be parsed.
        """
        request = aun_pb2.AunReloadMapRequest()
        response = self._invoke("ReloadMap", request, aun_pb2.AunReloadMapResponse())
        if response.error:
            raise EconetError(response.error)

    def add_map_peer(
        self, net: int, stn: int, host: str, port: int = 32768, label: str = ""
    ) -> None:
        """Add or replace a peer in the server's ``aun-map.json``.

        The server writes its own file (it may be on another host) atomically,
        preserving entry order and unknown keys, then applies the change to its
        peer set at once; other instances pick it up from their poll.

        Args:
            net: Econet network number (0-255).
            stn: Econet station number (1-254).
            host: IPv4 literal or DNS name.
            port: UDP port (1-65535).
            label: Optional note stored with the entry.

        Raises:
            EconetError: On a validation or write error (the message names the
                field), or when the map file is disabled (``map-file=none``).
        """
        request = aun_pb2.AunAddMapPeerRequest(
            net=net, stn=stn, host=host, port=port, label=label
        )
        response = self._invoke(
            "AddMapPeer", request, aun_pb2.AunAddMapPeerResponse()
        )
        if not response.success:
            raise EconetError(response.error)

    def remove_map_peer(self, net: int, stn: int) -> bool:
        """Remove a peer from the map file. Returns whether an entry was removed.

        Raises:
            EconetError: On a read or write error, or when the map file is
                disabled.
        """
        request = aun_pb2.AunRemoveMapPeerRequest(net=net, stn=stn)
        response = self._invoke(
            "RemoveMapPeer", request, aun_pb2.AunRemoveMapPeerResponse()
        )
        if not response.success:
            raise EconetError(response.error)
        return response.removed

    def add_map_subnet(self, net: int, subnet: str, label: str = "") -> None:
        """Add or replace the subnet rule for ``net`` (``a.b.c.0/24``) in the map file.

        A rule maps every station on the net to ``a.b.c.<station>`` port
        32768, the convention RISC OS and other AUN implementations use.

        Raises:
            EconetError: On a validation or write error, or when the map file
                is disabled.
        """
        request = aun_pb2.AunAddMapSubnetRequest(net=net, subnet=subnet, label=label)
        response = self._invoke(
            "AddMapSubnet", request, aun_pb2.AunAddMapSubnetResponse()
        )
        if not response.success:
            raise EconetError(response.error)

    def remove_map_subnet(self, net: int) -> bool:
        """Remove a subnet rule from the map file. Returns whether one was removed.

        Raises:
            EconetError: On a read or write error, or when the map file is
                disabled.
        """
        request = aun_pb2.AunRemoveMapSubnetRequest(net=net)
        response = self._invoke(
            "RemoveMapSubnet", request, aun_pb2.AunRemoveMapSubnetResponse()
        )
        if not response.success:
            raise EconetError(response.error)
        return response.removed

    def list_map(self) -> MapListing:
        """List the map file's entries with labels and host resolution.

        Distinct from :attr:`peers`, which lists the live resolved routing
        table. Empty when the map file is disabled.

        Raises:
            EconetError: If the file was present but could not be parsed.
        """
        request = aun_pb2.AunListMapRequest()
        response = self._invoke("ListMap", request, aun_pb2.AunListMapResponse())
        if response.error:
            raise EconetError(response.error)
        return MapListing(
            peers=[
                MapPeer(
                    net=p.net,
                    stn=p.stn,
                    host=p.host,
                    port=p.port,
                    label=p.label,
                    resolved=p.resolved,
                    resolved_ip=p.resolved_ip,
                )
                for p in response.peers
            ],
            subnets=[
                MapSubnet(net=s.net, subnet=s.subnet, label=s.label)
                for s in response.subnets
            ],
        )

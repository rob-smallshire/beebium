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

"""Transport-agnostic Econet hardware management for the beebium client.

Transport-specific operations (peer table, cable plug, status query)
live on the relevant transport service:

  - AUN: see ``beebium.ext.econet.aun.Aun`` (wraps AunService)
  - Piconet: see ``beebium.ext.econet.piconet.Piconet`` (wraps PiconetService)

Use ``Beebium.transport`` (``EconetTransport``) to discover which one
is active.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass

from beebium.client._proto import econet_pb2, econet_pb2_grpc
from beebium.client.exceptions import EconetError


@dataclass(frozen=True)
class AdlcStatus:
    """MC6854 ADLC register state."""

    cr1: int
    cr2: int
    cr3: int
    cr4: int
    sr1: int
    sr2: int
    irq_output: bool
    tx_fifo_empty: bool
    tx_fifo_full: bool
    rx_fifo_empty: bool
    rx_fifo_full: bool
    tx_frame_field: str
    rx_frame_field: str
    pse_level: int
    cts_input: bool


@dataclass(frozen=True)
class HandshakeStatus:
    """Four-way handshake state."""

    stage: str
    flag_fill_active: bool

    # Inbound holding queue. Packets arriving while the handshake is in a
    # stage that cannot use them are held and re-offered rather than dropped.
    # ``frames_dropped`` being non-zero means a peer is offering traffic
    # faster than the handshake can consume it.
    frames_held: int = 0
    frames_redelivered: int = 0
    frames_expired: int = 0
    frames_dropped: int = 0


@dataclass(frozen=True)
class EconetStatus:
    """Transport-agnostic Econet hardware status.

    For AUN-specific status (port, peer count) use
    ``beebium.ext.econet.aun.Aun.status``. For Piconet-specific status (device
    path, serial open) use ``beebium.ext.econet.piconet.Piconet.status``.
    """

    has_econet_socket: bool
    enabled: bool
    station_id: int
    aun_mode: bool
    connected: bool
    adlc: AdlcStatus | None
    handshake: HandshakeStatus | None
    tick_count: int = 0
    cr1_0x82_write_count: int = 0
    rx_frames_received_count: int = 0
    rx_blocked_by_reset_count: int = 0
    scout_ack_generated_count: int = 0
    tx_frames_from_beeb_count: int = 0
    unexpected_tx_reset_count: int = 0
    tx_from_idle_count: int = 0
    max_handshake_timer_seen: int = 0
    watchdog_timeout_count: int = 0
    send_stage_log: str = ""
    ticks_with_timer_active: int = 0
    read_stretch_parasite_ticks: int = 0


def _response_to_status(response: econet_pb2.GetEconetStatusResponse) -> EconetStatus:
    """Convert a GetEconetStatusResponse proto into an EconetStatus dataclass."""
    adlc = None
    if response.HasField("adlc"):
        a = response.adlc
        adlc = AdlcStatus(
            cr1=a.cr1,
            cr2=a.cr2,
            cr3=a.cr3,
            cr4=a.cr4,
            sr1=a.sr1,
            sr2=a.sr2,
            irq_output=a.irq_output,
            tx_fifo_empty=a.tx_fifo_empty,
            tx_fifo_full=a.tx_fifo_full,
            rx_fifo_empty=a.rx_fifo_empty,
            rx_fifo_full=a.rx_fifo_full,
            tx_frame_field=a.tx_frame_field,
            rx_frame_field=a.rx_frame_field,
            pse_level=a.pse_level,
            cts_input=a.cts_input,
        )

    handshake = None
    if response.HasField("handshake"):
        h = response.handshake
        handshake = HandshakeStatus(
            stage=h.stage,
            flag_fill_active=h.flag_fill_active,
            frames_held=h.frames_held,
            frames_redelivered=h.frames_redelivered,
            frames_expired=h.frames_expired,
            frames_dropped=h.frames_dropped,
        )

    return EconetStatus(
        has_econet_socket=response.has_econet_socket,
        enabled=response.enabled,
        station_id=response.station_id,
        aun_mode=response.aun_mode,
        connected=response.connected,
        adlc=adlc,
        handshake=handshake,
        tick_count=response.tick_count,
        cr1_0x82_write_count=response.cr1_0x82_write_count,
        rx_frames_received_count=response.rx_frames_received_count,
        rx_blocked_by_reset_count=response.rx_blocked_by_reset_count,
        scout_ack_generated_count=response.scout_ack_generated_count,
        tx_frames_from_beeb_count=response.tx_frames_from_beeb_count,
        unexpected_tx_reset_count=response.unexpected_tx_reset_count,
        tx_from_idle_count=response.tx_from_idle_count,
        max_handshake_timer_seen=response.max_handshake_timer_seen,
        watchdog_timeout_count=response.watchdog_timeout_count,
        send_stage_log=response.send_stage_log,
        ticks_with_timer_active=response.ticks_with_timer_active,
        read_stretch_parasite_ticks=response.read_stretch_parasite_ticks,
    )


class Econet:
    """Transport-agnostic Econet hardware management.

    Provides access to Econet hardware fitting / removal, station ID,
    and the transport-agnostic status query (ADLC registers, handshake
    state, frame counters).

    Transport-specific operations (peer table, cable plug, port info)
    live on a separate client class -- see ``beebium.ext.econet.aun.Aun`` and
    ``beebium.ext.econet.piconet.Piconet``.

    Usage:
        # Enable Econet with station ID 254 (binds an AUN socket)
        port = bbc.econet.enable(station_id=254)

        # Discover which transport is active
        active = bbc.transport.active
        if active and active.name == "aun":
            bbc.extensions[Aun].add_peer(net=0, stn=1, ip_address="192.168.1.100")

        # Check the generic status
        status = bbc.econet.status
        print(f"Station {status.station_id}, enabled={status.enabled}")

        # Disable
        bbc.econet.disable()
    """

    def __init__(self, stub: econet_pb2_grpc.EconetServiceStub):
        """Create an Econet interface.

        Args:
            stub: The gRPC stub for the EconetService.
        """
        self._stub = stub

    @property
    def status(self) -> EconetStatus:
        """Get Econet hardware status."""
        request = econet_pb2.GetEconetStatusRequest()
        response = self._stub.GetEconetStatus(request)
        return _response_to_status(response)

    def watch_status(self, *, min_interval_ms: int = 0) -> Iterator[EconetStatus]:
        """Stream Econet status changes.

        The server pushes an initial snapshot on subscription, then a new
        snapshot whenever status visible on EconetService changes
        (enable/disable, station ID change, or transport backend
        connection toggle). The stream stays open until the client
        cancels or the server shuts down.

        Args:
            min_interval_ms: Minimum interval between pushes (0 = server
                default, typically 50ms). The server only pushes on
                change, so this caps update rate rather than forcing
                periodic traffic.

        Yields:
            EconetStatus for each change.
        """
        request = econet_pb2.WatchEconetStatusRequest(min_interval_ms=min_interval_ms)
        for response in self._stub.WatchEconetStatus(request):
            yield _response_to_status(response)

    @property
    def is_enabled(self) -> bool:
        """True if Econet hardware is currently fitted."""
        return self.status.enabled

    @property
    def station_id(self) -> int:
        """Station number (1-254, 0 if disabled)."""
        return self.status.station_id

    def enable(
        self,
        station_id: int,
        *,
        aun_port: int = 0,
        no_network: bool = False,
    ) -> int:
        """Fit Econet hardware and optionally bind AUN socket.

        Args:
            station_id: Station number (1-254).
            aun_port: UDP port to bind (0 = use default 32768).
            no_network: If True, fit hardware with no network connection.

        Returns:
            Actual AUN port bound (useful when 0/default was requested).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.EnableEconetRequest(
            station_id=station_id,
            aun_port=aun_port,
            no_network=no_network,
        )
        response = self._stub.EnableEconet(request)
        if not response.success:
            raise EconetError(response.error)
        return response.actual_aun_port

    def set_station_id(self, station_id: int) -> None:
        """Set the station number (takes effect on next machine reset).

        On the Model B this is equivalent to changing the 8 address links.
        The NFS ROM re-reads the station number on Ctrl-Break.

        Args:
            station_id: Station number (1-254).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.SetStationIdRequest(station_id=station_id)
        response = self._stub.SetStationId(request)
        if not response.success:
            raise EconetError(response.error)

    def disable(self) -> None:
        """Remove Econet hardware (disable station, close AUN socket).

        Raises:
            EconetError: If the operation fails.
        """
        request = econet_pb2.DisableEconetRequest()
        response = self._stub.DisableEconet(request)
        if not response.success:
            raise EconetError(response.error)

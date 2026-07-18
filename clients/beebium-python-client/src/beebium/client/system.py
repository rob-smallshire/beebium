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

"""System information and server status for the beebium client."""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING

from beebium.client._proto import system_pb2, system_pb2_grpc

if TYPE_CHECKING:
    pass


class ServerStatus(Enum):
    """Server status types."""

    READY = "ready"
    SHUTTING_DOWN = "shutting_down"
    IDENTITY_CHANGED = "identity_changed"
    SHUTDOWN_PROGRESS = "shutdown_progress"
    HEARTBEAT = "heartbeat"


class ShutdownMode(Enum):
    """Shutdown mode for RequestShutdown RPC."""

    GRACEFUL = "graceful"  # Normal: notify clients, wait for subsystems, terminate
    IMMEDIATE = "immediate"  # Skip coordination, terminate ASAP


@dataclass(frozen=True)
class Provenance:
    """Launch provenance information.

    Identifies who/what launched the emulator core and when.
    """

    type: str  # e.g., "python-client", "macos-gui", "terminal"
    instance_uuid: str  # RFC 4122 UUID
    version: str  # Version of the launching client
    timestamp: int  # Unix timestamp (seconds since epoch)


class MachineIdentity:
    """Machine identity - UUID and mutable name.

    The `name` property can be read and written. Writing triggers
    a gRPC call to update the server-side name.

    Attributes:
        uuid: RFC 4122 v4 UUID, stable for machine lifetime (read-only)
        name: User-assignable label (read/write)
        model_type: e.g., "ModelB" (read-only)
        model_name: e.g., "BBC Model B" (read-only)
        system: Reference to the parent System object (read-only)
    """

    def __init__(
        self,
        uuid: str,
        name: str,
        model_type: str,
        model_name: str,
        system: System,
    ):
        self._uuid = uuid
        self._name = name
        self._model_type = model_type
        self._model_name = model_name
        self._system = system

    @property
    def uuid(self) -> str:
        """RFC 4122 v4 UUID, stable for machine lifetime."""
        return self._uuid

    @property
    def name(self) -> str:
        """User-assignable machine label."""
        return self._name

    @name.setter
    def name(self, value: str) -> None:
        """Set machine name via gRPC."""
        request = system_pb2.SetMachineNameRequest(name=value)
        response = self._system._stub.SetMachineName(request)
        self._name = response.identity.name

    @property
    def model_type(self) -> str:
        """Machine model type identifier (immutable)."""
        return self._model_type

    @property
    def model_name(self) -> str:
        """Human-readable model name (immutable)."""
        return self._model_name

    @property
    def system(self) -> System:
        """Reference to the parent System object."""
        return self._system

    def __repr__(self) -> str:
        return (
            f"MachineIdentity(uuid={self._uuid!r}, name={self._name!r}, "
            f"model_type={self._model_type!r}, model_name={self._model_name!r})"
        )

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, MachineIdentity):
            return NotImplemented
        return (
            self._uuid == other._uuid
            and self._name == other._name
            and self._model_type == other._model_type
            and self._model_name == other._model_name
        )


@dataclass(frozen=True)
class ShutdownResponse:
    """Response from RequestShutdown RPC."""

    accepted: bool  # Whether the shutdown request was accepted
    message: str  # Human-readable explanation


@dataclass(frozen=True)
class ShutdownConditionStatus:
    """Status of a subsystem preparing for shutdown."""

    name: str  # Subsystem name (e.g., "Disc controller")
    ready: bool  # True if ready for shutdown
    elapsed_ms: int  # Time since shutdown initiated
    timeout_ms: int  # Timeout for this condition


@dataclass(frozen=True)
class AdvertisementState:
    """mDNS service advertisement state."""

    enabled: bool  # Currently advertising
    available: bool  # Platform has mDNS support
    advertised_name: str  # Actual name being advertised (may differ due to collisions)


@dataclass(frozen=True)
class ServerStatusEvent:
    """Server status change event."""

    status: ServerStatus
    message: str
    shutdown_grace_ms: int  # Grace period for SHUTTING_DOWN
    identity: MachineIdentity | None = None  # For IDENTITY_CHANGED events
    shutdown_conditions: tuple[ShutdownConditionStatus, ...] = ()  # For SHUTDOWN_PROGRESS


@dataclass(frozen=True)
class PacingStats:
    """Snapshot of emulation pacing and speed headroom.

    The speed fields are sampled by the server over its pacing window
    (~5 seconds), so they update slowly and steadily -- intended for a
    polled readout (e.g. a UI speed slider once per second), not a
    high-frequency series.
    """

    ticks_executed: int  # Total pacing ticks since start
    ticks_io_skipped: int  # Ticks where sleep was cut short for I/O
    controller_drift: float  # Current drift in cycles (+ = ahead of real time)
    controller_integral: float  # Accumulated drift (time debt)
    speed_multiplier: float  # Configured multiplier (0.0 = unlimited)
    achieved_speed_multiplier: float  # Actual emulated rate / base clock over the window
    estimated_max_speed_multiplier: float  # Estimated ceiling at current host load; 0.0 = no estimate yet


class System:
    """System information and server status.

    Provides access to machine identification and server lifecycle events.

    Usage:
        # Get machine identity
        identity = bbc.system.identity
        print(f"Machine: {identity.model_name}")
        print(f"Name: {identity.name}")

        # Change machine name (triggers gRPC call)
        identity.name = "My BBC Micro"

        # Watch for status changes
        for event in bbc.system.watch_status():
            if event.status == ServerStatus.SHUTTING_DOWN:
                print(f"Server shutting down in {event.shutdown_grace_ms}ms")
                break
            elif event.status == ServerStatus.IDENTITY_CHANGED:
                print(f"Machine renamed to: {event.identity.name}")
    """

    def __init__(
        self,
        stub: system_pb2_grpc.SystemServiceStub,
        instance_uuid: str | None = None,
    ):
        """Create a System interface.

        Args:
            stub: The gRPC stub for the SystemService.
            instance_uuid: Optional client instance UUID for shutdown authorization.
                If provided, this is sent with RequestShutdown to authorize
                the request if it matches the server's launch provenance.
        """
        self._stub = stub
        self._instance_uuid = instance_uuid
        self._identity_cache: MachineIdentity | None = None
        self._provenance_cache: Provenance | None = None

    @property
    def identity(self) -> MachineIdentity:
        """Get machine identity.

        Returns a MachineIdentity object whose `name` property
        can be read or written (writing updates the server).
        """
        if self._identity_cache is None:
            request = system_pb2.GetSystemInfoRequest()
            response = self._stub.GetSystemInfo(request)
            self._identity_cache = MachineIdentity(
                uuid=response.identity.uuid,
                name=response.identity.name,
                model_type=response.identity.model_type,
                model_name=response.identity.model_name,
                system=self,
            )
        return self._identity_cache

    @property
    def provenance(self) -> Provenance:
        """Get launch provenance information (cached).

        Returns information about who/what launched this server instance
        and when it was launched.
        """
        if self._provenance_cache is None:
            request = system_pb2.GetSystemInfoRequest()
            response = self._stub.GetSystemInfo(request)
            prov = response.provenance
            self._provenance_cache = Provenance(
                type=prov.type,
                instance_uuid=prov.instance_uuid,
                version=prov.version,
                timestamp=prov.timestamp,
            )
        return self._provenance_cache

    def watch_status(self) -> Iterator[ServerStatusEvent]:
        """Stream server status events.

        Server sends READY immediately upon subscription, then status changes.
        Stream ends when server shuts down or client disconnects.

        Yields:
            ServerStatusEvent for each status change.
        """
        request = system_pb2.WatchServerStatusRequest()
        for response in self._stub.WatchServerStatus(request):
            identity = None
            conditions: tuple[ShutdownConditionStatus, ...] = ()

            if response.status == system_pb2.SERVER_STATUS_READY:
                status = ServerStatus.READY
            elif response.status == system_pb2.SERVER_STATUS_SHUTTING_DOWN:
                status = ServerStatus.SHUTTING_DOWN
            elif response.status == system_pb2.SERVER_STATUS_IDENTITY_CHANGED:
                status = ServerStatus.IDENTITY_CHANGED
                # Create MachineIdentity from the response
                identity = MachineIdentity(
                    uuid=response.identity.uuid,
                    name=response.identity.name,
                    model_type=response.identity.model_type,
                    model_name=response.identity.model_name,
                    system=self,
                )
                # Update cache
                self._identity_cache = identity
            elif response.status == system_pb2.SERVER_STATUS_HEARTBEAT:
                status = ServerStatus.HEARTBEAT
            elif response.status == system_pb2.SERVER_STATUS_SHUTDOWN_PROGRESS:
                status = ServerStatus.SHUTDOWN_PROGRESS
                # Parse shutdown condition status
                conditions = tuple(
                    ShutdownConditionStatus(
                        name=c.name,
                        ready=c.ready,
                        elapsed_ms=c.elapsed_ms,
                        timeout_ms=c.timeout_ms,
                    )
                    for c in response.shutdown_conditions
                )
            else:
                # Unknown status, treat as ready
                status = ServerStatus.READY

            yield ServerStatusEvent(
                status=status,
                message=response.message,
                shutdown_grace_ms=response.shutdown_grace_ms,
                identity=identity,
                shutdown_conditions=conditions,
            )

    @property
    def clock_speed_hz(self) -> int:
        """Nominal CPU clock frequency in Hz (e.g. 2000000 for 2 MHz)."""
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return response.clock_speed_hz

    @property
    def protocol_fingerprint(self) -> str:
        """The wire-protocol fingerprint the server was built against.

        Empty if the server predates protocol fingerprinting.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return response.protocol_fingerprint

    @property
    def executable_path(self) -> str:
        """Filesystem path of the running server executable.

        Resolved by the server from the OS rather than argv[0], so a server
        started through a PATH symlink reports its real target. Use this to
        identify which binary a connection actually reached -- notably when
        diagnosing a protocol fingerprint mismatch.

        Empty if the server cannot determine its own path.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return response.executable_path

    @property
    def client_count(self) -> int:
        """Number of clients with active WatchServerStatus streams.

        This count reflects how many clients are currently watching
        the server status. It does not include clients that are merely
        connected but not watching.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return response.connections.client_count

    def wait_for_ready(self, timeout: float = 5.0) -> bool:
        """Block until server sends READY status.

        Args:
            timeout: Maximum time to wait in seconds.

        Returns:
            True if server is ready, False on timeout.
        """
        import time

        deadline = time.monotonic() + timeout

        for event in self.watch_status():
            if event.status == ServerStatus.READY:
                return True
            if time.monotonic() >= deadline:
                return False

        return False

    def request_shutdown(
        self,
        mode: ShutdownMode = ShutdownMode.GRACEFUL,
        grace_period_ms: int = 5000,
    ) -> ShutdownResponse:
        """Request server shutdown.

        The server will accept the request if any of these conditions are met:
        - This client's instance UUID matches the server's launch provenance
        - Only one client is connected (this client owns the server)
        - Server was started with --allow-shutdown flag

        Args:
            mode: Shutdown mode (GRACEFUL or IMMEDIATE).
            grace_period_ms: Grace period in milliseconds for graceful shutdown.
                Clients watching server status will be notified with this grace period.
                Only meaningful for GRACEFUL mode. Default is 5000ms.

        Returns:
            ShutdownResponse with accepted status and message.

        Example:
            # Request graceful shutdown
            response = bbc.system.request_shutdown()
            if response.accepted:
                print("Shutdown initiated")
            else:
                print(f"Shutdown refused: {response.message}")

            # Request immediate shutdown (skip subsystem coordination)
            response = bbc.system.request_shutdown(mode=ShutdownMode.IMMEDIATE)
        """
        # Map Python enum to protobuf enum
        if mode == ShutdownMode.GRACEFUL:
            proto_mode = system_pb2.SHUTDOWN_GRACEFUL
        else:
            proto_mode = system_pb2.SHUTDOWN_IMMEDIATE

        request = system_pb2.ShutdownRequest(
            mode=proto_mode,
            grace_period_ms=grace_period_ms,
        )

        # Build metadata with instance UUID if available
        metadata: list[tuple[str, str]] = []
        if self._instance_uuid:
            metadata.append(("x-beebium-instance-uuid", self._instance_uuid))

        response = self._stub.RequestShutdown(request, metadata=tuple(metadata))

        return ShutdownResponse(
            accepted=response.accepted,
            message=response.message,
        )

    def get_advertisement_state(self) -> AdvertisementState:
        """Get current mDNS service advertisement state.

        Returns:
            AdvertisementState with current enabled/available status.

        Example:
            state = bbc.system.get_advertisement_state()
            if state.available:
                if state.enabled:
                    print(f"Advertising as: {state.advertised_name}")
                else:
                    print("Advertisement available but disabled")
            else:
                print("mDNS not available on this platform")
        """
        request = system_pb2.GetAdvertisementStateRequest()
        response = self._stub.GetAdvertisementState(request)
        return AdvertisementState(
            enabled=response.state.enabled,
            available=response.state.available,
            advertised_name=response.state.advertised_name,
        )

    def set_advertisement(self, enabled: bool) -> AdvertisementState:
        """Enable or disable mDNS service advertisement.

        Args:
            enabled: True to start advertising, False to stop.

        Returns:
            AdvertisementState with resulting state after the operation.
            Note: enabled may be False even if requested True, if mDNS
            is not available on the platform.

        Example:
            # Start advertising
            state = bbc.system.set_advertisement(enabled=True)
            if state.enabled:
                print(f"Now advertising as: {state.advertised_name}")
            elif not state.available:
                print("mDNS not available on this platform")

            # Stop advertising
            bbc.system.set_advertisement(enabled=False)
        """
        request = system_pb2.SetAdvertisementRequest(enabled=enabled)
        response = self._stub.SetAdvertisement(request)
        return AdvertisementState(
            enabled=response.state.enabled,
            available=response.state.available,
            advertised_name=response.state.advertised_name,
        )

    def set_speed_multiplier(self, speed_multiplier: float) -> float:
        """Set the runtime emulation speed multiplier.

        Args:
            speed_multiplier: Runtime speed multiplier.
                `0.0` means unlimited speed, `1.0` is real-time.

        Returns:
            The resulting runtime speed multiplier echoed by the server.
        """
        request = system_pb2.SetSpeedMultiplierRequest(
            speed_multiplier=speed_multiplier,
        )
        response = self._stub.SetSpeedMultiplier(request)
        return response.speed_multiplier

    def get_pacing_stats(self) -> PacingStats:
        """Return a snapshot of emulation pacing and speed headroom.

        ``estimated_max_speed_multiplier`` is an upper-bound estimate of the
        fastest the host can currently run the emulation, derived from the
        proportion of wall-clock time spent computing rather than sleeping.
        It is ``0.0`` when no estimate is available yet (the emulator is
        idle/paused, or the first pacing window has not elapsed).

        The fields are sampled over the server's pacing window (~5s), so
        poll this at a low rate (e.g. once per second for a speed slider)
        rather than treating it as a high-frequency stream.
        """
        request = system_pb2.GetPacingStatsRequest()
        response = self._stub.GetPacingStats(request)
        return PacingStats(
            ticks_executed=response.ticks_executed,
            ticks_io_skipped=response.ticks_io_skipped,
            controller_drift=response.controller_drift,
            controller_integral=response.controller_integral,
            speed_multiplier=response.speed_multiplier,
            achieved_speed_multiplier=response.achieved_speed_multiplier,
            estimated_max_speed_multiplier=response.estimated_max_speed_multiplier,
        )

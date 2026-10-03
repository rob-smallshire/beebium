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
from beebium.client.host import is_local_host

if TYPE_CHECKING:
    pass


class ServerStatus(Enum):
    """Server status types."""

    READY = "ready"
    SHUTTING_DOWN = "shutting_down"
    IDENTITY_CHANGED = "identity_changed"
    SHUTDOWN_PROGRESS = "shutdown_progress"
    HEARTBEAT = "heartbeat"
    MACHINE_RESET = "machine_reset"


class ResetKind(Enum):
    """How a machine reset was performed (for MACHINE_RESET events)."""

    UNSPECIFIED = "unspecified"
    SOFT = "soft"  # Break key
    HARD = "hard"  # Power-on / Ctrl-Break / Reset RPC


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
    """Machine identity - UUID, name template and rendered name.

    A machine's name is a *template*: ordinary text in which ``{key}`` stands
    for the current value of a placeholder the server provides, such as
    ``"Station {econet-station} (AUN, Model B)"``. The server renders it into
    the *name* to show, and re-renders it (about once a second) as the values
    change. A template without braces is a plain name and renders as itself.

    Which placeholders exist, what they mean and what their values are belong
    to the server: ask :meth:`System.list_name_placeholders`, and use
    :meth:`System.preview_machine_name` to see how a template would render.

    The `name` and `name_template` properties can be read and written. Writing
    either sends the new template to the server and updates both from its
    reply; to learn which placeholders did not render, use
    :meth:`System.set_machine_name` instead.

    Attributes:
        uuid: RFC 4122 v4 UUID, stable for machine lifetime (read-only)
        name: The rendered name to show (read/write; writing sets the template)
        name_template: The name as the user edits it (read/write)
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
        *,
        name_template: str | None = None,
    ):
        """Create a MachineIdentity.

        Args:
            uuid: The machine's UUID.
            name: The rendered name.
            model_type: Machine model type identifier.
            model_name: Human-readable model name.
            system: The parent System object, used to rename.
            name_template: The template `name` was rendered from. Defaults to
                `name` itself, since a plain name is a template of itself.
        """
        self._uuid = uuid
        self._name = name
        self._name_template = name if name_template is None else name_template
        self._model_type = model_type
        self._model_name = model_name
        self._system = system

    @classmethod
    def _from_proto(cls, identity: system_pb2.MachineIdentity, system: System) -> MachineIdentity:
        return cls(
            uuid=identity.uuid,
            name=identity.name,
            name_template=identity.name_template,
            model_type=identity.model_type,
            model_name=identity.model_name,
            system=system,
        )

    @property
    def uuid(self) -> str:
        """RFC 4122 v4 UUID, stable for machine lifetime."""
        return self._uuid

    @property
    def name(self) -> str:
        """The name to show: the name template rendered by the server.

        This is what window titles, the network announcement and status
        events carry. It is a snapshot: it changes on the server when a
        placeholder's value does, and a fresh identity arrives with each
        IDENTITY_CHANGED event from :meth:`System.watch_status`.
        """
        return self._name

    @name.setter
    def name(self, value: str) -> None:
        """Set the machine's name template via gRPC (a plain name is a template)."""
        self._adopt(self._system.set_machine_name(value).identity)

    @property
    def name_template(self) -> str:
        """The name as the user edits it, from which `name` is rendered.

        ``{key}`` stands for a placeholder's current value; ``{{`` and ``}}``
        are literal braces. A key the server does not know renders verbatim,
        and a placeholder that does not apply to this machine renders empty.
        """
        return self._name_template

    @name_template.setter
    def name_template(self, value: str) -> None:
        """Set the machine's name template via gRPC."""
        self._adopt(self._system.set_machine_name(value).identity)

    def _adopt(self, other: MachineIdentity) -> None:
        self._name = other._name
        self._name_template = other._name_template

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
            f"name_template={self._name_template!r}, "
            f"model_type={self._model_type!r}, model_name={self._model_name!r})"
        )

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, MachineIdentity):
            return NotImplemented
        return (
            self._uuid == other._uuid
            and self._name == other._name
            and self._name_template == other._name_template
            and self._model_type == other._model_type
            and self._model_name == other._model_name
        )


@dataclass(frozen=True)
class NamePlaceholder:
    """A placeholder a machine name template can use, and its value here.

    Records come from the server (:meth:`System.list_name_placeholders`);
    the client knows no keys of its own. A picker shows `label` under
    `group` with the current `value`, and inserts `insertion` -- the server
    supplies the insertion text so that clients need not build it.
    """

    key: str  # Used in templates as {key}, e.g. "econet-station"; never renamed once shipped
    label: str  # Short human name for a picker, e.g. "Econet station"
    description: str  # One sentence on what it shows and when it changes
    group: str  # Picker heading, e.g. "Econet"
    insertion: str  # The text to insert into a template, e.g. "{econet-station}"
    value: str  # Current value on this machine; empty when not applicable
    applicable: bool  # False when this machine cannot have a value (renders empty)


@dataclass(frozen=True)
class MachineNamePreview:
    """How a name template renders now, without changing anything.

    The three report fields say what the rendering could not substitute, so
    a client can warn without parsing the template. All are empty when the
    template rendered fully.
    """

    name: str  # The rendering, as MachineIdentity.name would be with this template
    unknown_keys: tuple[str, ...] = ()  # Inner text of each {...} that is not a known key (rendered verbatim)
    inapplicable_keys: tuple[str, ...] = ()  # Known keys that do not apply to this machine (rendered empty)
    malformed: tuple[str, ...] = ()  # Fragments rendered literally: an unterminated "{..." or a lone "}"


@dataclass(frozen=True)
class MachineNameChange:
    """The result of setting a machine's name template.

    The template is applied even when parts of it did not render; the report
    fields (as in :class:`MachineNamePreview`) say which, so a client can warn.
    """

    identity: MachineIdentity  # The updated identity: the template and its rendering
    unknown_keys: tuple[str, ...] = ()  # Inner text of each {...} that is not a known key (rendered verbatim)
    inapplicable_keys: tuple[str, ...] = ()  # Known keys that do not apply to this machine (rendered empty)
    malformed: tuple[str, ...] = ()  # Fragments rendered literally: an unterminated "{..." or a lone "}"


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
    reset_kind: ResetKind = ResetKind.UNSPECIFIED  # For MACHINE_RESET events


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

        # A name can be a template of server-provided placeholders
        for placeholder in bbc.system.list_name_placeholders():
            print(f"{placeholder.insertion}: {placeholder.label} = {placeholder.value!r}")
        preview = bbc.system.preview_machine_name("Station {econet-station}")
        print(f"Would be: {preview.name}, unknown: {preview.unknown_keys}")
        change = bbc.system.set_machine_name("Station {econet-station}")
        print(f"Now: {change.identity.name} from {change.identity.name_template}")

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
        self._provenance_cache: Provenance | None = None

    @property
    def identity(self) -> MachineIdentity:
        """Get machine identity, as the server has it now.

        Returns a MachineIdentity object whose `name` and `name_template`
        properties can be read or written (writing updates the server).
        Each read asks the server, because the rendered name changes on its
        own as placeholder values change; the object returned is a snapshot.
        To follow changes as they happen, watch for IDENTITY_CHANGED events
        from :meth:`watch_status`.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return MachineIdentity._from_proto(response.identity, self)

    def set_machine_name(self, name_template: str) -> MachineNameChange:
        """Set the machine's name template.

        The server renders the template into the machine's name at once and
        keeps it current as placeholder values change. A plain name is a
        template with no placeholders. The template is applied even if some
        of it does not render; the result reports what did not, so a caller
        can warn.

        Args:
            name_template: The new template, e.g. "Station {econet-station}".
                It must not be empty: the server rejects an empty template
                (grpc.RpcError with status INVALID_ARGUMENT).

        Returns:
            MachineNameChange with the updated identity and the unknown keys,
            inapplicable keys and malformed fragments in the template.

        Example:
            change = bbc.system.set_machine_name("Station {econet-station}")
            if change.unknown_keys:
                print(f"Not known to this server: {change.unknown_keys}")
            print(change.identity.name)  # e.g. "Station 80"
        """
        request = system_pb2.SetMachineNameRequest(name_template=name_template)
        response = self._stub.SetMachineName(request)
        return MachineNameChange(
            identity=MachineIdentity._from_proto(response.identity, self),
            unknown_keys=tuple(response.unknown_keys),
            inapplicable_keys=tuple(response.inapplicable_keys),
            malformed=tuple(response.malformed),
        )

    def preview_machine_name(self, name_template: str) -> MachineNamePreview:
        """Render a name template against current values without changing anything.

        Use this for a live preview while a name is being edited, rather than
        implementing the template syntax in the client.

        Args:
            name_template: The template to render.

        Returns:
            MachineNamePreview with the rendering and what it could not
            substitute.
        """
        request = system_pb2.PreviewMachineNameRequest(name_template=name_template)
        response = self._stub.PreviewMachineName(request)
        return MachineNamePreview(
            name=response.name,
            unknown_keys=tuple(response.unknown_keys),
            inapplicable_keys=tuple(response.inapplicable_keys),
            malformed=tuple(response.malformed),
        )

    def list_name_placeholders(self) -> tuple[NamePlaceholder, ...]:
        """List the placeholders a name template can use on this server.

        The set is the server's -- its core machine, its Econet socket and
        any loaded extensions each contribute -- so it varies between
        servers and versions. Each record carries its current value on this
        machine, which is a snapshot: call again to refresh.

        Returns:
            The placeholders, in the server's stable order.

        Example:
            for p in bbc.system.list_name_placeholders():
                state = p.value if p.applicable else "(not applicable)"
                print(f"[{p.group}] {p.insertion} {p.label}: {state}")
        """
        request = system_pb2.ListNamePlaceholdersRequest()
        response = self._stub.ListNamePlaceholders(request)
        return tuple(
            NamePlaceholder(
                key=p.key,
                label=p.label,
                description=p.description,
                group=p.group,
                insertion=p.insertion,
                value=p.value,
                applicable=p.applicable,
            )
            for p in response.placeholders
        )

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
            reset_kind = ResetKind.UNSPECIFIED

            if response.status == system_pb2.SERVER_STATUS_READY:
                status = ServerStatus.READY
            elif response.status == system_pb2.SERVER_STATUS_SHUTTING_DOWN:
                status = ServerStatus.SHUTTING_DOWN
            elif response.status == system_pb2.SERVER_STATUS_IDENTITY_CHANGED:
                status = ServerStatus.IDENTITY_CHANGED
                identity = MachineIdentity._from_proto(response.identity, self)
            elif response.status == system_pb2.SERVER_STATUS_HEARTBEAT:
                status = ServerStatus.HEARTBEAT
            elif response.status == system_pb2.SERVER_STATUS_MACHINE_RESET:
                status = ServerStatus.MACHINE_RESET
                if response.reset_kind == system_pb2.RESET_KIND_SOFT:
                    reset_kind = ResetKind.SOFT
                elif response.reset_kind == system_pb2.RESET_KIND_HARD:
                    reset_kind = ResetKind.HARD
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
                reset_kind=reset_kind,
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
    def host_fingerprint(self) -> str:
        """An opaque token identifying the host the server is running on.

        Compare it with a token derived the same way on this host to learn
        whether the two processes share a filesystem -- which is what any
        exchange of paths depends on. :meth:`Disc.Drive.insert` sends a path
        for the server to open, and a drive's ``disc_url`` is a path on the
        server, so both are meaningless across hosts.

        Network addresses cannot answer this: a client may reach a server on
        its own machine over loopback, over that machine's LAN address, or
        through a Bonjour ".local" name that resolves back to itself.

        The token is ``sha256("beebium-host-v1:" + host_identifier)`` in
        lowercase hex, where the host identifier is ``gethostuuid()`` on
        macOS, ``/etc/machine-id`` on Linux and the ``MachineGuid`` registry
        value on Windows. Deriving this host's own is left to the caller: an
        automation script is usually running alongside its server and does
        not need to ask. See docs/frontend-local-server-gating.md.

        Empty if the server's host will not identify itself, which must be
        read as "unknown" rather than as matching another empty value.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return response.host_fingerprint

    @property
    def is_local(self) -> bool:
        """Whether the server is running on this same host.

        True when the two processes share a filesystem, which is what any
        exchange of paths depends on: :meth:`Drive.insert` sends a path for
        the server to open, and a drive's ``disc_url`` is a path on the
        server's host.

        False if the server's host will not identify itself, or this one
        will not: an unknown is never taken for a match, since that would
        licence exactly the path exchanges that break.

        This is a fact about a connection, not a permission. Nothing in this
        API is withheld when it is False -- inserting by path against a
        server elsewhere remains available, and will simply fail if the path
        means nothing there. Use it to decide whether offering a path is
        sensible, as the macOS front end does.
        """
        return is_local_host(self.host_fingerprint)

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

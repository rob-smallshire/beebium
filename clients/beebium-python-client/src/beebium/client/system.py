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

import os
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
    GUEST_READY = "guest_ready"  # the guest asked for typed input after a reset


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
    """Machine identity - UUID, plain name and live description.

    A machine's *name* is plain text, never a template: what titles and the
    network announcement show. A saved machine's is its document's name (its
    directory's name without ``.beebium-machine``), so renaming it with
    :meth:`System.set_machine_name` renames the document; a machine not yet
    saved is simply renamed.

    Its *description* is shown beside the name (a window subtitle) and is a
    *template*: ordinary text in which ``{key}`` stands for the current value
    of a placeholder the server provides, such as
    ``"Station {econet-station} (AUN)"``. The server renders it and re-renders
    it (about once a second) as the values change. Which placeholders exist
    belongs to the server: ask :meth:`System.list_description_placeholders`,
    and use :meth:`System.preview_machine_description` to see how a template
    would render.

    Attributes:
        uuid: RFC 4122 v4 UUID, stable for machine lifetime (read-only)
        name: The machine's name, plain text (read/write; writing renames)
        description: The rendered description; empty for none (read-only)
        description_template: The description as the user edits it
            (read/write; writing sets it)
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
        description: str = "",
        description_template: str = "",
    ):
        """Create a MachineIdentity.

        Args:
            uuid: The machine's UUID.
            name: The machine's name.
            model_type: Machine model type identifier.
            model_name: Human-readable model name.
            system: The parent System object, used to rename and describe.
            description: The rendered description.
            description_template: The template `description` was rendered from.
        """
        self._uuid = uuid
        self._name = name
        self._description = description
        self._description_template = description_template
        self._model_type = model_type
        self._model_name = model_name
        self._system = system

    @classmethod
    def _from_proto(cls, identity: system_pb2.MachineIdentity, system: System) -> MachineIdentity:
        return cls(
            uuid=identity.uuid,
            name=identity.name,
            description=identity.description,
            description_template=identity.description_template,
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
        """The machine's name, plain text: what titles and the network
        announcement show. A snapshot; a fresh identity arrives with each
        IDENTITY_CHANGED event from :meth:`System.watch_status`."""
        return self._name

    @name.setter
    def name(self, value: str) -> None:
        """Rename the machine via gRPC (a saved machine's document with it)."""
        self._adopt(self._system.set_machine_name(value))

    @property
    def description(self) -> str:
        """The description, rendered by the server from its template; empty
        when there is none. A snapshot: it changes on the server when a
        placeholder's value does."""
        return self._description

    @property
    def description_template(self) -> str:
        """The description as the user edits it, from which `description` is
        rendered. ``{key}`` stands for a placeholder's current value; ``{{``
        and ``}}`` are literal braces. A key the server does not know renders
        verbatim, and a placeholder that does not apply to this machine
        renders empty."""
        return self._description_template

    @description_template.setter
    def description_template(self, value: str) -> None:
        """Set the machine's description template via gRPC."""
        self._adopt(self._system.set_machine_description(value).identity)

    def _adopt(self, other: MachineIdentity) -> None:
        self._name = other._name
        self._description = other._description
        self._description_template = other._description_template

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
            f"description={self._description!r}, "
            f"description_template={self._description_template!r}, "
            f"model_type={self._model_type!r}, model_name={self._model_name!r})"
        )

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, MachineIdentity):
            return NotImplemented
        return (
            self._uuid == other._uuid
            and self._name == other._name
            and self._description == other._description
            and self._description_template == other._description_template
            and self._model_type == other._model_type
            and self._model_name == other._model_name
        )


@dataclass(frozen=True)
class DescriptionPlaceholder:
    """A placeholder a machine description template can use, and its value here.

    Records come from the server (:meth:`System.list_description_placeholders`);
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
class DescriptionTemplateReport:
    """What a description template's rendering could not substitute.

    Lets a client warn without parsing the template. All three are empty when
    the template rendered fully.
    """

    unknown_keys: tuple[str, ...] = ()  # Inner text of each {...} that is not a known key (rendered verbatim)
    inapplicable_keys: tuple[str, ...] = ()  # Known keys that do not apply to this machine (rendered empty)
    malformed: tuple[str, ...] = ()  # Fragments rendered literally: an unterminated "{..." or a lone "}"

    @classmethod
    def _from_proto(cls, proto) -> DescriptionTemplateReport:
        return cls(
            unknown_keys=tuple(proto.unknown_keys),
            inapplicable_keys=tuple(proto.inapplicable_keys),
            malformed=tuple(proto.malformed),
        )


@dataclass(frozen=True)
class MachineDescriptionPreview:
    """How a description template renders now, without changing anything."""

    description: str  # The rendering, as MachineIdentity.description would be with this template
    report: DescriptionTemplateReport = DescriptionTemplateReport()  # What it could not substitute


@dataclass(frozen=True)
class MachineDescriptionChange:
    """The result of setting a machine's description template.

    The template is applied even when parts of it did not render; the report
    says which, so a client can warn.
    """

    identity: MachineIdentity  # The updated identity: the template and its rendering
    report: DescriptionTemplateReport = DescriptionTemplateReport()  # What it could not substitute


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
    # On every event: a machine not yet saved has written to the copies of the
    # media its template named, which are lost unless it is saved.
    template_media_written: bool = False
    # On every event: the guest has asked for typed input since its last reset
    # (see Keyboard.typing_status()); GUEST_READY reports it becoming true.
    guest_ready: bool = False


@dataclass(frozen=True)
class SavedMachine:
    """Where a machine is after a Save, and its identity there."""

    machine_dirpath: str  # The machine's directory now
    identity: MachineIdentity  # A Save As gives the machine a new uuid


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

        # Rename a machine not yet saved (triggers gRPC call)
        identity.name = "My BBC Micro"

        # A description is a template of server-provided placeholders
        for placeholder in bbc.system.list_description_placeholders():
            print(f"{placeholder.insertion}: {placeholder.label} = {placeholder.value!r}")
        preview = bbc.system.preview_machine_description("Station {econet-station}")
        print(f"Would be: {preview.description}, unknown: {preview.report.unknown_keys}")
        change = bbc.system.set_machine_description("Station {econet-station}")
        print(f"Now: {change.identity.description} from {change.identity.description_template}")

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

        Returns a MachineIdentity object whose `name` and
        `description_template` properties can be read or written (writing
        updates the server). Each read asks the server, because the rendered
        description changes on its own as placeholder values change; the
        object returned is a snapshot.
        To follow changes as they happen, watch for IDENTITY_CHANGED events
        from :meth:`watch_status`.
        """
        request = system_pb2.GetSystemInfoRequest()
        response = self._stub.GetSystemInfo(request)
        return MachineIdentity._from_proto(response.identity, self)

    def set_machine_name(self, name: str) -> MachineIdentity:
        """Rename the machine.

        A name is plain text, never a template. A saved machine is named by
        its document, so its document is renamed: its directory moves to
        ``<name>.beebium-machine`` beside it while it runs, and
        :attr:`machine_dirpath` reports the new one. A name that cannot name
        a file on every host is refused (grpc.RpcError with status
        INVALID_ARGUMENT, saying why), as is one whose directory already
        exists (ALREADY_EXISTS). An empty name is refused with
        INVALID_ARGUMENT.

        Returns:
            The updated identity.
        """
        request = system_pb2.SetMachineNameRequest(name=name)
        response = self._stub.SetMachineName(request)
        return MachineIdentity._from_proto(response.identity, self)

    def set_machine_description(self, description_template: str) -> MachineDescriptionChange:
        """Set the machine's description template (empty for none).

        The server renders the template into the description at once and
        keeps it current as placeholder values change. The template is
        applied even if some of it does not render; the result reports what
        did not, so a caller can warn. A saved machine keeps it in its
        directory.

        Returns:
            MachineDescriptionChange with the updated identity and a report of
            the unknown keys, inapplicable keys and malformed fragments.

        Example:
            change = bbc.system.set_machine_description("Station {econet-station}")
            if change.report.unknown_keys:
                print(f"Not known to this server: {change.report.unknown_keys}")
            print(change.identity.description)  # e.g. "Station 80"
        """
        request = system_pb2.SetMachineDescriptionRequest(description_template=description_template)
        response = self._stub.SetMachineDescription(request)
        return MachineDescriptionChange(
            identity=MachineIdentity._from_proto(response.identity, self),
            report=DescriptionTemplateReport._from_proto(response.report),
        )

    def preview_machine_description(self, description_template: str) -> MachineDescriptionPreview:
        """Render a description template against current values without
        changing anything, for a live preview while it is being edited.

        Returns:
            MachineDescriptionPreview with the rendering and what it could not
            substitute.
        """
        request = system_pb2.PreviewMachineDescriptionRequest(description_template=description_template)
        response = self._stub.PreviewMachineDescription(request)
        return MachineDescriptionPreview(
            description=response.description,
            report=DescriptionTemplateReport._from_proto(response.report),
        )

    def list_description_placeholders(self) -> tuple[DescriptionPlaceholder, ...]:
        """List the placeholders a description template can use on this server.

        The set is the server's -- its core machine, its Econet socket and
        any loaded extensions each contribute -- so it varies between
        servers and versions. Each record carries its current value on this
        machine, which is a snapshot: call again to refresh.

        Returns:
            The placeholders, in the server's stable order.

        Example:
            for p in bbc.system.list_description_placeholders():
                state = p.value if p.applicable else "(not applicable)"
                print(f"[{p.group}] {p.insertion} {p.label}: {state}")
        """
        request = system_pb2.ListDescriptionPlaceholdersRequest()
        response = self._stub.ListDescriptionPlaceholders(request)
        return tuple(
            DescriptionPlaceholder(
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
            elif response.status == system_pb2.SERVER_STATUS_GUEST_READY:
                status = ServerStatus.GUEST_READY
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
                template_media_written=response.template_media_written,
                guest_ready=response.guest_ready,
            )

    @property
    def machine_dirpath(self) -> str:
        """The machine's directory on the server's host: a saved machine's, or
        the transient one a machine not yet saved runs in. Empty if none."""
        request = system_pb2.GetSystemInfoRequest()
        return self._stub.GetSystemInfo(request).machine_dirpath

    @property
    def machine_saved(self) -> bool:
        """True for a saved machine, whose directory outlives the server."""
        request = system_pb2.GetSystemInfoRequest()
        return self._stub.GetSystemInfo(request).machine_saved

    def save_machine(
        self, destination_dirpath: str | os.PathLike[str] = "", *, replace_existing: bool = False
    ) -> SavedMachine:
        """Save the machine without stopping it.

        A machine not yet saved is saved into ``destination_dirpath``, keeping
        its uuid. A saved machine with no destination writes its state in
        place; with one, it is copied there with a new uuid and runs on from
        the copy (Save As). The destination must not exist, unless
        ``replace_existing``, when it may be a saved machine no process is
        running (not this one), which the new one replaces once it is whole.
        Paths are on the server's host.

        Raises:
            grpc.RpcError: INVALID_ARGUMENT for a machine not yet saved and no
                destination, ALREADY_EXISTS for a destination that exists,
                FAILED_PRECONDITION for one that cannot be replaced, INTERNAL
                when the directory cannot be written.
        """
        request = system_pb2.SaveMachineRequest(
            destination_dirpath=os.fspath(destination_dirpath), replace_existing=replace_existing
        )
        response = self._stub.SaveMachine(request)
        return SavedMachine(
            machine_dirpath=response.machine_dirpath,
            identity=MachineIdentity._from_proto(response.identity, self),
        )

    def move_machine(self, destination_dirpath: str | os.PathLike[str]) -> SavedMachine:
        """Move a saved machine's directory without stopping it.

        ``destination_dirpath`` is the new directory in full, ending in
        ``<name>.beebium-machine``; its parent must exist. The machine keeps
        its uuid, runs on from there, and is named ``<name>``; the old
        directory is gone. Moving to its own directory changes nothing. The
        path is on the server's host.

        Raises:
            grpc.RpcError: FAILED_PRECONDITION for a machine not yet saved,
                INVALID_ARGUMENT for a destination that cannot be a machine
                document or lies inside the machine's directory, NOT_FOUND
                when its parent does not exist, ALREADY_EXISTS when it exists,
                INTERNAL when the move fails (the machine stays where it was).
        """
        request = system_pb2.MoveMachineRequest(destination_dirpath=os.fspath(destination_dirpath))
        response = self._stub.MoveMachine(request)
        return SavedMachine(
            machine_dirpath=response.machine_dirpath,
            identity=MachineIdentity._from_proto(response.identity, self),
        )

    def reset_battery(self) -> tuple[str, ...]:
        """Forget the machine's battery-backed state, as taking out the battery
        does, from the next start. Returns the units forgotten, e.g.
        ``("integra-b-board-ram", "integra-b-rtc")`` or ``("clock/saf3019p",)``
        for an extension's."""
        response = self._stub.ResetBattery(system_pb2.ResetBatteryRequest())
        return tuple(response.reset_units)

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

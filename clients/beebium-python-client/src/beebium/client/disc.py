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

"""Floppy disc drive management for the beebium client."""

from __future__ import annotations

import threading
import time
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from urllib.parse import urlparse

from beebium.client._proto import disc_pb2, disc_pb2_grpc
from beebium.client.exceptions import DiscError


class DriveState(Enum):
    """Unified drive state."""

    EMPTY = "empty"
    LOADED = "loaded"
    EJECTING = "ejecting"


class DiscEventType(Enum):
    """Type of disc event."""

    INSERTED = "inserted"
    EJECTED = "ejected"
    FORCE_EJECTED = "force_ejected"
    EJECT_REQUESTED = "eject_requested"
    EJECT_CANCELLED = "eject_cancelled"
    MOTOR_ON = "motor_on"
    MOTOR_OFF = "motor_off"


@dataclass(frozen=True)
class DiscMetadata:
    """Information about a disc image."""

    name: str
    sides: int  # 1 for SSD, 2 for DSD
    write_protected: bool
    format: str  # e.g. "SSD", "ADL", "HFE v3"


@dataclass(frozen=True)
class DriveStatus:
    """Status of a single floppy drive."""

    drive: int  # 0 or 1
    state: DriveState
    disc_name: str
    disc_url: str
    motor_on: bool
    current_track: int
    write_protected: bool
    disc: DiscMetadata | None


@dataclass(frozen=True)
class DiscControllerStatus:
    """Overall disc subsystem status."""

    has_disc_controller: bool
    controller_type: str  # "WD1770", "8271", ""
    is_socketed: bool
    installed_controller_id: str
    drives: tuple[DriveStatus, ...]


@dataclass
class DiscEvent:
    """A disc subsystem event."""

    type: DiscEventType
    drive: int
    timestamp_cycles: int
    disc: DiscMetadata | None
    disc_url: str


@dataclass(frozen=True)
class DiscControllerInfo:
    """Information about an available disc controller type."""

    id: str  # "acorn-1770"
    display_name: str  # "Acorn 1770"
    fdc_chip: str  # "WD1770"
    description: str


class EventStreamHandle:
    """Handle for a background event stream."""

    def __init__(self, thread: threading.Thread, stop_event: threading.Event):
        """Create an event stream handle.

        Args:
            thread: The background thread running the stream.
            stop_event: Event to signal the stream to stop.
        """
        self._thread = thread
        self._stop_event = stop_event

    def stop(self, timeout: float = 1.0) -> None:
        """Stop the background stream.

        Args:
            timeout: Maximum time to wait for the thread to stop (seconds).
        """
        self._stop_event.set()
        self._thread.join(timeout)

    @property
    def is_running(self) -> bool:
        """True if the stream is still running."""
        return self._thread.is_alive()


def _proto_state_to_drive_state(state: int) -> DriveState:
    """Convert proto DiscDriveState enum to DriveState."""
    if state == disc_pb2.DISC_DRIVE_STATE_EMPTY:
        return DriveState.EMPTY
    elif state == disc_pb2.DISC_DRIVE_STATE_LOADED:
        return DriveState.LOADED
    elif state == disc_pb2.DISC_DRIVE_STATE_EJECTING:
        return DriveState.EJECTING
    else:
        return DriveState.EMPTY


def _proto_event_type_to_event_type(event_type: int) -> DiscEventType:
    """Convert proto DiscEventType enum to DiscEventType."""
    mapping: dict[int, DiscEventType] = {
        disc_pb2.DISC_EVENT_INSERTED: DiscEventType.INSERTED,
        disc_pb2.DISC_EVENT_EJECTED: DiscEventType.EJECTED,
        disc_pb2.DISC_EVENT_FORCE_EJECTED: DiscEventType.FORCE_EJECTED,
        disc_pb2.DISC_EVENT_EJECT_REQUESTED: DiscEventType.EJECT_REQUESTED,
        disc_pb2.DISC_EVENT_EJECT_CANCELLED: DiscEventType.EJECT_CANCELLED,
        disc_pb2.DISC_EVENT_MOTOR_ON: DiscEventType.MOTOR_ON,
        disc_pb2.DISC_EVENT_MOTOR_OFF: DiscEventType.MOTOR_OFF,
    }
    return mapping.get(event_type, DiscEventType.INSERTED)


def _proto_metadata_to_disc_metadata(proto: disc_pb2.DiscMetadata) -> DiscMetadata:
    """Convert proto DiscMetadata to DiscMetadata."""
    return DiscMetadata(
        name=proto.name,
        sides=proto.sides,
        write_protected=proto.write_protected,
        format=proto.format,
    )


def _path_to_url(path_or_url: str | Path) -> str:
    """Convert a path or URL to a file:// URL."""
    if isinstance(path_or_url, Path):
        return path_or_url.resolve().as_uri()

    # Check if it's already a URL
    parsed = urlparse(path_or_url)
    if parsed.scheme:
        return path_or_url

    # It's a path, convert to URL
    return Path(path_or_url).resolve().as_uri()


class Drive:
    """Single drive interface for convenient access.

    Usage:
        # Insert a disc
        bbc.disc.drive(0).insert("/path/to/game.ssd")

        # Check status
        if bbc.disc.drive(0).is_loaded:
            print(f"Disc: {bbc.disc.drive(0).name}")

        # Eject with safe eject
        bbc.disc.drive(0).eject()
    """

    def __init__(self, disc_service: Disc, drive_num: int):
        """Create a Drive interface.

        Args:
            disc_service: The parent Disc service.
            drive_num: The drive number (0 or 1).
        """
        self._disc = disc_service
        self._drive_num = drive_num

    @property
    def status(self) -> DriveStatus:
        """Get current drive status."""
        controller_status = self._disc.status
        for drive in controller_status.drives:
            if drive.drive == self._drive_num:
                return drive
        # Return a default empty status if drive not found
        return DriveStatus(
            drive=self._drive_num,
            state=DriveState.EMPTY,
            disc_name="",
            disc_url="",
            motor_on=False,
            current_track=0,
            write_protected=False,
            disc=None,
        )

    @property
    def state(self) -> DriveState:
        """Current drive state."""
        return self.status.state

    @property
    def is_empty(self) -> bool:
        """True if no disc is in the drive."""
        return self.state == DriveState.EMPTY

    @property
    def is_loaded(self) -> bool:
        """True if a disc is loaded and idle."""
        return self.state == DriveState.LOADED

    @property
    def is_ejecting(self) -> bool:
        """True if an eject is pending."""
        return self.state == DriveState.EJECTING

    @property
    def motor_on(self) -> bool:
        """True if the drive motor is running."""
        return self.status.motor_on

    @property
    def current_track(self) -> int:
        """Current head position (0-79)."""
        return self.status.current_track

    @property
    def name(self) -> str:
        """Disc name (empty string if no disc)."""
        return self.status.disc_name

    @property
    def disc(self) -> DiscMetadata | None:
        """Full disc metadata, or None if empty."""
        return self.status.disc

    def insert(
        self,
        path_or_url: str | Path,
        write_protect: bool = False,
    ) -> DiscMetadata:
        """Insert a disc image.

        The drive must be empty. Removing a disc is a deliberate
        :meth:`eject`, so that it goes through the safe eject path rather
        than being pulled out from under a spinning motor.

        Args:
            path_or_url: Local path or file:// URL to disc image.
            write_protect: Force write-protect regardless of file permissions.

        Returns:
            Metadata of inserted disc.

        Raises:
            DiscError: If the drive already holds a disc, or the image
                cannot be loaded.
        """
        url = _path_to_url(path_or_url)
        request = disc_pb2.InsertDiscRequest(
            drive=self._drive_num,
            url=url,
            write_protect_override=write_protect,
        )
        response = self._disc._stub.InsertDisc(request)
        if not response.success:
            raise DiscError(f"Failed to insert disc: {response.error}")
        return _proto_metadata_to_disc_metadata(response.disc)

    def eject(
        self,
        immediate: bool = False,
        quiescence_ms: int = 500,
    ) -> None:
        """Eject the disc (safe eject by default).

        A safe eject waits for the motor to have been off for
        ``quiescence_ms`` and then completes. It waits for as long as that
        takes: the server never gives up and pulls the disc out of a
        spinning drive on its own. Ending the wait is this caller's
        decision -- eject again with ``immediate=True`` to force it, or
        :meth:`cancel_eject` to leave the disc where it is.

        Args:
            immediate: Skip the quiescence wait and eject now, whatever the
                drive is doing.
            quiescence_ms: Motor-off time required (default 500).

        Raises:
            DiscError: If drive is empty or request fails.
        """
        request = disc_pb2.EjectDiscRequest(
            drive=self._drive_num,
            immediate=immediate,
            quiescence_ms=quiescence_ms,
        )
        response = self._disc._stub.EjectDisc(request)
        if not response.accepted:
            raise DiscError(f"Failed to eject disc: {response.error}")

    def cancel_eject(self) -> None:
        """Abandon a pending safe eject, leaving the disc in the drive.

        Raises:
            DiscError: If no eject was pending.
        """
        request = disc_pb2.CancelEjectRequest(drive=self._drive_num)
        response = self._disc._stub.CancelEject(request)
        if not response.cancelled:
            raise DiscError(f"Failed to cancel eject: {response.error}")

    def wait_for_eject(self, timeout: float = 15.0) -> bool:
        """Block until the disc has been ejected.

        A safe eject completes when the drive falls quiet, and waits for as
        long as that takes -- the server never gives up on its own. So a
        False here does not mean the eject failed; it means the drive is
        still busy and will stay pending until this caller decides
        otherwise, by forcing it (``eject(immediate=True)``) or abandoning
        it (:meth:`cancel_eject`).

        Args:
            timeout: Maximum time to wait in seconds. An arbitrary bound on
                this caller's patience, not a property of the eject.

        Returns:
            True if ejected, False if still pending when the timeout expired.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.is_empty:
                return True
            time.sleep(0.05)
        return False


class Disc:
    """Floppy disc drive management.

    Provides access to floppy disc drives with insert/eject operations,
    status queries, and event streaming.

    Usage:
        # Quick access via drive helpers
        bbc.disc.drive(0).insert("game.ssd")
        bbc.disc.drive(1).insert("save.ssd")

        # Check controller status
        status = bbc.disc.status
        print(f"Controller: {status.controller_type}")

        # List available controllers (for socketed machines)
        for ctrl in bbc.disc.available_controllers:
            print(f"{ctrl.id}: {ctrl.display_name}")

        # Install a controller
        bbc.disc.install_controller("acorn-1770")

        # Stream events
        for event in bbc.disc.events():
            print(f"Drive {event.drive}: {event.type.value}")
    """

    def __init__(self, stub: disc_pb2_grpc.DiscServiceStub):
        """Create a Disc interface.

        Args:
            stub: The gRPC stub for the DiscService.
        """
        self._stub = stub
        self._drive0: Drive | None = None
        self._drive1: Drive | None = None

    def drive(self, num: int) -> Drive:
        """Get a Drive interface for drive 0 or 1.

        Args:
            num: Drive number (0 or 1).

        Returns:
            Drive interface for the specified drive.

        Raises:
            ValueError: If num is not 0 or 1.
        """
        if num == 0:
            return self.drive0
        elif num == 1:
            return self.drive1
        else:
            raise ValueError(f"Drive number must be 0 or 1, got {num}")

    @property
    def drive0(self) -> Drive:
        """Shortcut for drive(0)."""
        if self._drive0 is None:
            self._drive0 = Drive(self, 0)
        return self._drive0

    @property
    def drive1(self) -> Drive:
        """Shortcut for drive(1)."""
        if self._drive1 is None:
            self._drive1 = Drive(self, 1)
        return self._drive1

    @property
    def status(self) -> DiscControllerStatus:
        """Get overall disc controller status."""
        request = disc_pb2.GetDriveStatusRequest()
        response = self._stub.GetDriveStatus(request)

        drives = []
        for proto_drive in response.drives:
            disc_metadata = None
            if proto_drive.HasField("disc"):
                disc_metadata = _proto_metadata_to_disc_metadata(proto_drive.disc)

            drives.append(
                DriveStatus(
                    drive=proto_drive.drive,
                    state=_proto_state_to_drive_state(proto_drive.state),
                    disc_name=proto_drive.disc_name,
                    disc_url=proto_drive.disc_url,
                    motor_on=proto_drive.motor_on,
                    current_track=proto_drive.current_track,
                    write_protected=proto_drive.write_protected,
                    disc=disc_metadata,
                )
            )

        return DiscControllerStatus(
            has_disc_controller=response.has_disc_controller,
            controller_type=response.controller_type,
            is_socketed=response.is_socketed,
            installed_controller_id=response.installed_controller_id,
            drives=tuple(drives),
        )

    @property
    def has_controller(self) -> bool:
        """True if machine has a disc controller."""
        return self.status.has_disc_controller

    @property
    def controller_type(self) -> str:
        """Controller type name (e.g., "WD1770")."""
        return self.status.controller_type

    @property
    def is_socketed(self) -> bool:
        """True if controller can be changed at runtime."""
        return self.status.is_socketed

    @property
    def available_controllers(self) -> list[DiscControllerInfo]:
        """List available controller types (for socketed machines)."""
        request = disc_pb2.ListAvailableControllersRequest()
        response = self._stub.ListAvailableControllers(request)
        return [
            DiscControllerInfo(
                id=ctrl.id,
                display_name=ctrl.display_name,
                fdc_chip=ctrl.fdc_chip,
                description=ctrl.description,
            )
            for ctrl in response.controllers
        ]

    def install_controller(self, controller_id: str) -> str:
        """Install a disc controller (socketed machines only).

        Args:
            controller_id: CLI identifier (e.g., "acorn-1770", "none").

        Returns:
            Name of installed controller type.

        Raises:
            DiscError: If installation fails.
        """
        request = disc_pb2.InstallDiscControllerRequest(controller_id=controller_id)
        response = self._stub.InstallDiscController(request)
        if not response.success:
            raise DiscError(f"Failed to install controller: {response.error}")
        return response.controller_type

    @property
    def spin_up_delay_enabled(self) -> bool:
        """Check if motor spin-up delay is enabled."""
        request = disc_pb2.GetSpinUpDelayRequest()
        response = self._stub.GetSpinUpDelay(request)
        return response.enabled

    def set_spin_up_delay(self, enabled: bool) -> None:
        """Enable/disable motor spin-up delay (for automation).

        Args:
            enabled: True to enable spin-up delay, False to disable.

        Raises:
            DiscError: If the operation fails.
        """
        request = disc_pb2.SetSpinUpDelayRequest(enabled=enabled)
        response = self._stub.SetSpinUpDelay(request)
        if not response.success:
            raise DiscError(f"Failed to set spin-up delay: {response.error}")

    def events(self) -> Iterator[DiscEvent]:
        """Stream disc events (insert, eject, motor changes).

        Yields:
            DiscEvent for each event.
        """
        request = disc_pb2.SubscribeDiscEventsRequest()
        for proto_event in self._stub.SubscribeDiscEvents(request):
            disc_metadata = None
            if proto_event.HasField("disc"):
                disc_metadata = _proto_metadata_to_disc_metadata(proto_event.disc)

            yield DiscEvent(
                type=_proto_event_type_to_event_type(proto_event.type),
                drive=proto_event.drive,
                timestamp_cycles=proto_event.timestamp_cycles,
                disc=disc_metadata,
                disc_url=proto_event.disc_url,
            )

    def start_background_events(self, callback: Callable[[DiscEvent], None]) -> EventStreamHandle:
        """Start streaming events in background thread.

        Args:
            callback: Callback invoked for each event.

        Returns:
            A handle that can be used to stop the stream.
        """
        stop_event = threading.Event()

        def stream_thread() -> None:
            try:
                for event in self.events():
                    if stop_event.is_set():
                        break
                    callback(event)
            except Exception:
                pass  # Stream ended or cancelled

        thread = threading.Thread(target=stream_thread, daemon=True)
        thread.start()
        return EventStreamHandle(thread, stop_event)

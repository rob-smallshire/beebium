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

"""Sideways ROM/RAM slot management for the Beebium client.

Wraps :class:`SidewaysService` for Python consumers - test suites,
automation scripts, and future tooling. Surfaces the same topology
and runtime state the macOS Memory sidebar uses: per-socket type
(ROM/RAM/empty), populated flag, source filepath, and the parsed
ROM header when one is recognised.

Reconfiguration via :meth:`Sideways.configure_slot` only works on
machines that expose runtime-configurable sockets (the ROM/RAM
expansion board today; future cartridge slots). Model B and Model B+
sockets are real chip sockets and return an error.
"""

from __future__ import annotations

import dataclasses
from collections.abc import Iterator
from enum import IntEnum
from typing import cast

from beebium.client._proto import sideways_pb2, sideways_pb2_grpc
from beebium.client.exceptions import BeebiumError


class SlotType(IntEnum):
    """The current type of a sideways slot.

    Mirrors :class:`sideways_pb2.SidewaysSlotType`. EMPTY slots read
    as 0xFF (open bus) and ignore writes; ROM slots return data and
    ignore writes; RAM slots return data and accept writes.
    """

    EMPTY = sideways_pb2.SIDEWAYS_SLOT_TYPE_EMPTY
    ROM = sideways_pb2.SIDEWAYS_SLOT_TYPE_ROM
    RAM = sideways_pb2.SIDEWAYS_SLOT_TYPE_RAM


@dataclasses.dataclass(frozen=True)
class RomHeader:
    """Parsed sideways ROM header for what is currently in a socket.

    Only emitted by the server when the standard "(C)" copyright
    marker is found - empty sockets, blank RAM, and non-standard
    images carry no ``RomHeader`` in the response.
    """

    title: str
    version: str
    copyright: str
    contains_romfs: bool
    # Any combination of "language", "service", "romfs" describing what
    # the ROM is. See docs/manuals/sidewrom.pdf and
    # docs/romfs-detection.md.
    kinds: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class SocketCapabilities:
    """What a physical socket can hold and whether it is runtime-reconfigurable."""

    supports_rom: bool
    supports_ram: bool
    supports_empty: bool
    runtime_configurable: bool
    # The socket has a RAM write-protect switch; only then can its RAM be
    # write-protected. False for sideways RAM with no such switch.
    supports_write_protect: bool = False


@dataclasses.dataclass(frozen=True)
class SocketStatus:
    """One physical sideways socket on the running machine."""

    socket_index: int
    label: str  # Physical label, e.g. "IC101" or "IC71"
    aliased_slots: tuple[int, ...]  # Logical slots this socket answers
    type: SlotType
    populated: bool
    # Absolute filepath used to load this slot's contents on the server,
    # or "" if loaded from raw bytes / not populated / not stored
    # per-slot (Model B+).
    image_filepath: str
    capabilities: SocketCapabilities
    rom_header: RomHeader | None
    # True when this is a RAM slot whose write-protect switch is engaged
    # (e.g. the ATPL Sidewise slot 15). Always False for ROM/empty slots
    # and machines with no write-protect control.
    write_protected: bool = False

    @property
    def priority(self) -> int:
        """The highest slot this socket answers - i.e. its boot priority.

        The MOS scans sideways slots from 15 downward looking for the
        language ROM, so the highest slot is the one the OS sees.
        """
        return max(self.aliased_slots) if self.aliased_slots else 0


@dataclasses.dataclass(frozen=True)
class MotherboardLink:
    """A motherboard jumper that affects sideways wiring.

    Currently only the Model B+'s ``S13`` (which selects whether IC71
    answers at slots 14/15 or 0/1).
    """

    name: str
    value: str
    description: str


@dataclasses.dataclass(frozen=True)
class SlotStatusReport:
    """A snapshot of the machine's sideways topology and runtime state."""

    has_aliasing: bool
    num_physical_slots: int
    sockets: tuple[SocketStatus, ...]
    motherboard_links: tuple[MotherboardLink, ...]

    def find_socket_for_slot(self, slot: int) -> SocketStatus | None:
        """The socket that responds at the given logical slot, or None."""
        for s in self.sockets:
            if slot in s.aliased_slots:
                return s
        return None


@dataclasses.dataclass(frozen=True)
class SlotConfiguredEvent:
    """Emitted after :meth:`Sideways.configure_slot` reconfigures a slot."""

    timestamp_cycles: int
    slot: int
    socket: int
    type: SlotType
    image_filepath: str


@dataclasses.dataclass(frozen=True)
class SlotHeaderChangedEvent:
    """Emitted by the live header scanner when the parsed ROM header of
    a RAM slot's current contents differs from the last value broadcast.

    Fires only for subscribers who opted in with
    ``monitor_header_changes=True`` on :meth:`Sideways.subscribe_events`.
    Eventually-consistent at ~1 Hz: typical end-to-end latency after the
    BBC writes a new ROM image (e.g. via ``*SRLOAD``) is well under two
    seconds. See ``docs/discussion/sideways-live-header-updates.md``.
    """

    timestamp_cycles: int
    slot: int
    rom_header: RomHeader


SidewaysEvent = SlotConfiguredEvent | SlotHeaderChangedEvent


class Sideways:
    """Client for the SidewaysService.

    Usage::

        status = bbc.sideways.get_slot_status()
        for socket in status.sockets:
            if socket.rom_header is not None:
                print(socket.label, socket.rom_header.title)

        for event in bbc.sideways.subscribe_events():
            ...  # SlotConfiguredEvent

        # Reconfigure (errors on Model B / Model B+ - chip sockets).
        bbc.sideways.configure_slot(7, SlotType.RAM)
    """

    def __init__(self, stub: sideways_pb2_grpc.SidewaysServiceStub):
        self._stub = stub

    def get_slot_status(self) -> SlotStatusReport:
        """Read the current sideways topology and per-socket state."""
        request = sideways_pb2.GetSlotStatusRequest()
        response = self._stub.GetSlotStatus(request)
        return _map_status(response)

    def configure_slot(
        self,
        slot: int,
        slot_type: SlotType,
        *,
        url: str | None = None,
        data: bytes | None = None,
    ) -> int:
        """Reconfigure ``slot`` to ``slot_type``, optionally loading an image.

        ``url`` and ``data`` are mutually exclusive. For ``SlotType.ROM``
        / ``SlotType.RAM`` either source is optional (empty ROM / blank
        RAM respectively if omitted). For ``SlotType.EMPTY`` any image
        argument is ignored.

        Returns the physical socket index that was configured. For
        aliased machines (Model B), this is the socket the slot maps
        to; for non-aliased machines it equals ``slot``.

        Raises:
            ValueError: If both ``url`` and ``data`` are given.
            BeebiumError: If the server rejects the request - most
                commonly because the socket is not runtime-configurable
                (Model B and Model B+ chip sockets).
        """
        if url is not None and data is not None:
            raise ValueError("Pass exactly one of url= or data=")

        request = sideways_pb2.ConfigureSlotRequest(
            slot=slot,
            type=cast("sideways_pb2.SidewaysSlotType.ValueType", int(slot_type)),
        )
        if url is not None:
            request.url = url
        elif data is not None:
            request.data = data

        response = self._stub.ConfigureSlot(request)
        if not response.success:
            raise BeebiumError(response.error or "ConfigureSlot failed")
        return response.actual_socket

    def set_write_protect(self, slot: int, write_protected: bool) -> bool:
        """Engage or release a RAM slot's write-protect switch.

        Models a board's write-protect switch (e.g. the ATPL Sidewise
        slot 15). Returns the slot's write-protect state after the call.

        Raises:
            BeebiumError: If the server rejects the request - most
                commonly because the slot is not RAM, or the machine has
                no write-protect control.
        """
        request = sideways_pb2.SetSlotWriteProtectRequest(
            slot=slot,
            write_protected=write_protected,
        )
        response = self._stub.SetSlotWriteProtect(request)
        if not response.success:
            raise BeebiumError(response.error or "SetSlotWriteProtect failed")
        return response.write_protected

    def read_slot_data(
        self,
        slot: int,
        offset: int = 0,
        length: int = 0,
    ) -> bytes:
        """Read raw bytes from a sideways slot.

        Args:
            slot: Slot number 0..15.
            offset: Offset within the slot (0..16383).
            length: Number of bytes to read (0 means to the end of the
                slot). Clamped to the slot boundary on the server.

        Returns:
            The requested bytes.

        Raises:
            BeebiumError: If the server reports an error (invalid slot,
                machine has no sideways memory, ...).
        """
        request = sideways_pb2.ReadSlotDataRequest(
            slot=slot,
            offset=offset,
            length=length,
        )
        response = self._stub.ReadSlotData(request)
        if not response.success:
            raise BeebiumError(response.error or "ReadSlotData failed")
        return response.data

    def subscribe_events(
        self,
        *,
        monitor_header_changes: bool = False,
        min_interval_ms: int = 0,
    ) -> Iterator[SidewaysEvent]:
        """Stream sideways events from the server.

        Yields typed events. The stream runs until the caller cancels
        (return / break out of the for-loop) or the server closes it.

        Args:
            monitor_header_changes: Opt in to
                :class:`SlotHeaderChangedEvent` at ~1 Hz for any RAM
                slot whose contents change. Off by default; costs the
                server a periodic rescan, so enable only while a
                consumer is actively interested (and drop the stream
                when they aren't).
            min_interval_ms: Reserved for future per-event-type rate
                control; currently unused.
        """
        request = sideways_pb2.SubscribeEventsRequest(
            min_interval_ms=min_interval_ms,
            monitor_header_changes=monitor_header_changes,
        )
        for event_pb in self._stub.SubscribeEvents(request):
            event = _map_event(event_pb)
            if event is not None:
                yield event


# --- Proto -> Python mapping ---


def _map_status(response: sideways_pb2.GetSlotStatusResponse) -> SlotStatusReport:
    return SlotStatusReport(
        has_aliasing=response.has_aliasing,
        num_physical_slots=response.num_physical_slots,
        sockets=tuple(_map_socket(s) for s in response.sockets),
        motherboard_links=tuple(
            MotherboardLink(
                name=link.name,
                value=link.value,
                description=link.description,
            )
            for link in response.motherboard_links
        ),
    )


def _map_socket(s: sideways_pb2.SocketStatus) -> SocketStatus:
    header: RomHeader | None = None
    if s.HasField("rom_header") and s.rom_header.recognised:
        header = RomHeader(
            title=s.rom_header.title,
            version=s.rom_header.version,
            copyright=s.rom_header.copyright,
            contains_romfs=s.rom_header.contains_romfs,
            kinds=tuple(s.rom_header.kinds),
        )
    return SocketStatus(
        socket_index=s.socket_index,
        label=s.socket_label,
        aliased_slots=tuple(s.aliased_slots),
        type=SlotType(s.type),
        populated=s.populated,
        image_filepath=s.image_name,
        capabilities=SocketCapabilities(
            supports_rom=s.capabilities.supports_rom,
            supports_ram=s.capabilities.supports_ram,
            supports_empty=s.capabilities.supports_empty,
            runtime_configurable=s.capabilities.runtime_configurable,
            supports_write_protect=s.capabilities.supports_write_protect,
        ),
        rom_header=header,
        write_protected=s.write_protected,
    )


def _map_event(event: sideways_pb2.SidewaysEvent) -> SidewaysEvent | None:
    which = event.WhichOneof("event")
    if which == "slot_configured":
        return SlotConfiguredEvent(
            timestamp_cycles=event.timestamp_cycles,
            slot=event.slot_configured.slot,
            socket=event.slot_configured.socket,
            type=SlotType(event.slot_configured.type),
            image_filepath=event.slot_configured.image_name,
        )
    if which == "slot_header_changed":
        header_pb = event.slot_header_changed.rom_header
        return SlotHeaderChangedEvent(
            timestamp_cycles=event.timestamp_cycles,
            slot=event.slot_header_changed.slot,
            rom_header=RomHeader(
                title=header_pb.title,
                version=header_pb.version,
                copyright=header_pb.copyright,
                contains_romfs=header_pb.contains_romfs,
                kinds=tuple(header_pb.kinds),
            ),
        )
    return None

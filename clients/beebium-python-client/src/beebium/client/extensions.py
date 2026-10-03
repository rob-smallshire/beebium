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

"""Discovery of the peripheral extensions a server has loaded.

Wraps the core ``PeripheralExtensionService``. This is the reflective entry
point for the extension model: it reports *what the running server actually
loaded* -- each extension's type name, instance id, current configuration, the
parameter schema from its manifest, any storage devices it publishes, and
whether it exposes an Extension UI.

This module provides discovery only (metadata). Binding a loaded extension to a
typed client adapter -- ``bbc.extensions[AcornRtc]`` / ``AcornRtc.attach(bbc)`` -- is a
separate concern layered on top; see
``docs/discussion/python-client-architecture.md``.
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass
from enum import IntEnum
from typing import TypeVar, overload

from beebium.client._proto import (
    peripheral_extension_pb2 as pe_pb2,
)
from beebium.client._proto import (
    peripheral_extension_pb2_grpc as pe_grpc,
)
from beebium.client.exceptions import ExtensionError
from beebium.client.extension import (
    PERIPHERAL_ENTRY_POINT_GROUP,
    ExtensionAdapter,
    PeripheralExtensionAdapter,
    create_adapter,
    resolve_loaded,
)
from beebium.client.extension_rpc import ExtensionChannel

# Bound to PeripheralExtensionAdapter: bbc.extensions[...] only resolves
# peripheral extensions. Handing it a transport adapter (e.g. Aun) is a static
# type error -- use bbc.transport[...] for those.
A = TypeVar("A", bound=PeripheralExtensionAdapter)


class StorageKind(IntEnum):
    """Whether a storage device's media is swappable at runtime."""

    # Non-removable media (hard discs, RAM discs): no eject/mount affordance.
    FIXED = pe_pb2.StorageDevice.FIXED
    # User-swappable media: future UIs expose mount/eject.
    REMOVABLE = pe_pb2.StorageDevice.REMOVABLE


@dataclass(frozen=True)
class StorageDevice:
    """A storage device published by a peripheral extension."""

    # Stable identifier, unique within the process.
    id: str
    # Device-class name, e.g. "Hard Disc Drive 0".
    name: str
    # Whether the media is fixed or removable.
    kind: StorageKind
    # Semantic media class: "hard-disc", "floppy", "ram-disc", ...
    media_type: str
    # Host path to the backing store (empty for empty removable media).
    backing_path: str
    # Name of this device's activity indicator in IndicatorService (may be empty).
    activity_indicator_name: str


@dataclass(frozen=True)
class ParameterSchemaInfo:
    """One configurable parameter from an extension's manifest schema."""

    key: str
    # "string", "integer", "boolean" or "filepath".
    type: str
    description: str
    # Positional index for CLI-style args; -1 means keyword-only.
    position: int
    required: bool
    default_value: str


@dataclass(frozen=True)
class ExtensionInfo:
    """A single loaded-and-initialised extension, as the server reports it."""

    # Extension type, from the manifest (kebab-case: "aun", "rpc-serial", ...).
    name: str
    # Instance identity (a UUID, or a user-provided id).
    id: str
    # Display name; falls back to id when the manifest gives none.
    label: str
    # Attachment points the extension occupies ("serial-port", "1mhz-bus", ...).
    attaches_to: tuple[str, ...]
    # Capabilities the extension provides ("scsi", ...).
    provides: tuple[str, ...]
    # Current instance configuration (resolved parameter values).
    config: dict[str, str]
    # Parameter schema from the manifest.
    parameters: tuple[ParameterSchemaInfo, ...]
    # Description from the manifest.
    description: str
    # True when the extension exposes an Extension UI control panel.
    has_ui: bool
    # Storage devices the extension publishes (usually empty).
    storage_devices: tuple[StorageDevice, ...]


def _storage_device_from_proto(proto: pe_pb2.StorageDevice) -> StorageDevice:
    return StorageDevice(
        id=proto.id,
        name=proto.name,
        kind=StorageKind(proto.kind),
        media_type=proto.media_type,
        backing_path=proto.backing_path,
        activity_indicator_name=proto.activity_indicator_name,
    )


def _parameter_schema_from_proto(proto: pe_pb2.ParameterSchemaInfo) -> ParameterSchemaInfo:
    return ParameterSchemaInfo(
        key=proto.key,
        type=proto.type,
        description=proto.description,
        position=proto.position,
        required=proto.required,
        default_value=proto.default_value,
    )


def _extension_info_from_proto(proto: pe_pb2.ExtensionInfo) -> ExtensionInfo:
    return ExtensionInfo(
        name=proto.name,
        id=proto.id,
        label=proto.label,
        attaches_to=tuple(proto.attaches_to),
        provides=tuple(proto.provides),
        # dict() iterates the protobuf map, avoiding the map find()/at() bug
        # noted in CLAUDE.md (x86_64 macOS).
        config=dict(proto.config),
        parameters=tuple(_parameter_schema_from_proto(p) for p in proto.parameters),
        description=proto.description,
        has_ui=proto.has_ui,
        storage_devices=tuple(_storage_device_from_proto(d) for d in proto.storage_devices),
    )


class Extensions:
    """Discover the peripheral extensions a server has loaded.

    Usage::

        for info in bbc.extensions.loaded:
            print(info.name, info.id, info.config)

        if "rpc-serial" in bbc.extensions:
            rpc = bbc.extensions.info("rpc-serial")
            print(rpc.parameters)

    Each access re-queries the server, so the result always reflects the
    current set of loaded extensions.

    Beyond discovery, this is the bridge to typed client adapters. Index by an
    adapter class for the concrete type (full autocompletion), or by name for
    the base type::

        rtc = bbc.extensions[AcornRtc]         # -> AcornRtc (or AcornRtc.attach(bbc))
        adapter = bbc.extensions["acorn-rtc"]  # -> ExtensionAdapter (generic)

    Econet transports (AUN, Piconet) are not peripherals; reach them with
    ``bbc.transport[Aun]``.
    """

    def __init__(
        self,
        stub: pe_grpc.PeripheralExtensionServiceStub,
        channel: ExtensionChannel,
    ):
        self._stub = stub
        self._channel = channel

    @property
    def loaded(self) -> list[ExtensionInfo]:
        """All loaded-and-initialised extensions, as the server reports them."""
        response = self._stub.ListExtensions(pe_pb2.ListExtensionsRequest())
        return [_extension_info_from_proto(e) for e in response.extensions]

    def info(self, name_or_id: str) -> ExtensionInfo | None:
        """The loaded extension whose ``name`` or ``id`` matches, else None.

        ``name`` (the manifest type) is matched first; ``id`` (instance
        identity) second. For singleton extensions the two coincide.
        """
        loaded = self.loaded
        for info in loaded:
            if info.name == name_or_id:
                return info
        for info in loaded:
            if info.id == name_or_id:
                return info
        return None

    def __iter__(self) -> Iterator[ExtensionInfo]:
        return iter(self.loaded)

    def __len__(self) -> int:
        return len(self.loaded)

    def __contains__(self, name_or_id: object) -> bool:
        if not isinstance(name_or_id, str):
            return False
        return self.info(name_or_id) is not None

    # -- Typed / generic adapter access --------------------------------------

    @overload
    def __getitem__(self, key: str) -> ExtensionAdapter: ...
    @overload
    def __getitem__(self, key: type[A]) -> A: ...

    def __getitem__(self, key: str | type[A]) -> ExtensionAdapter | A:
        """Return a client adapter for a loaded peripheral extension.

        A class key (``bbc.extensions[AcornRtc]``) returns that concrete adapter
        type. A string key resolves the installed adapter via the
        ``beebium.ext.peripheral`` registry, typed as the base
        ``ExtensionAdapter``; the string may be the manifest name
        (``"acorn-rtc"``) or a specific instance id.

        Raises:
            ExtensionNotLoadedError: if the server has not loaded the extension.
            ExtensionAmbiguousError: if a name matches several loaded instances
                (address one by its id instead).
            ExtensionAdapterNotInstalledError: (string key) if no adapter is
                registered for the resolved extension.
        """
        if isinstance(key, type):
            info = self._require_loaded(key.EXTENSION_NAME, requested=key.__name__, match_id=False)
            return key(info.name, self._channel, extension_id=info.id)
        info = self._require_loaded(key, requested=repr(key))
        # Create from the resolved manifest name (the entry-point key), bound to
        # the resolved instance id -- so a string that was an id works too.
        return create_adapter(
            info.name,
            PERIPHERAL_ENTRY_POINT_GROUP,
            self._channel,
            extension_id=info.id,
        )

    @overload
    def get(self, key: str, default: None = None) -> ExtensionAdapter | None: ...
    @overload
    def get(self, key: type[A], default: None = None) -> A | None: ...

    def get(self, key: str | type[A], default: ExtensionAdapter | A | None = None) -> ExtensionAdapter | A | None:
        """Like ``self[key]`` but returns ``default`` instead of raising.

        Returns ``default`` when the extension is not loaded on the server or
        (for a string key) when no adapter is installed for it.
        """
        try:
            return self[key]
        except ExtensionError:
            return default

    def _require_loaded(self, key: str, *, requested: str, match_id: bool = True) -> ExtensionInfo:
        return resolve_loaded(self.loaded, key, requested=requested, kind="extension", match_id=match_id)

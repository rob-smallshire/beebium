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
loaded* -- each extension's type name, instance id, current configuration,
where it is attached, the parameter schema from its manifest, any storage
devices it publishes, and whether it exposes an Extension UI -- and the
machine's attachment points by name.

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
class ParameterChoiceInfo:
    """One of the values a parameter or field allows."""

    # As written in a preset or on the command line.
    value: str
    label: str
    # What choosing it means; may be empty.
    description: str


@dataclass(frozen=True)
class ParameterFieldInfo:
    """One part of the items of a list parameter (an AUN peer's station, host, ...)."""

    key: str
    label: str
    description: str
    # "string", "integer" or "boolean".
    type: str
    required: bool
    default_value: str
    min: int | None
    max: int | None
    choices: tuple[ParameterChoiceInfo, ...]


@dataclass(frozen=True)
class ParameterSchemaInfo:
    """One configurable parameter from an extension's manifest schema."""

    key: str
    # "string", "integer", "boolean", "filepath" or "dirpath".
    type: str
    description: str
    # Positional index for CLI-style args; -1 means keyword-only.
    position: int
    required: bool
    default_value: str
    # Short human-readable name, for a form.
    label: str
    # The key may be given repeatedly.
    is_list: bool
    # Non-empty: the default is worked out on the server's host by this means.
    default_from: str
    # The value the extension uses when none is given, on the server's host.
    effective_default: str
    # "integer" only: the least and greatest values allowed, None when unbounded.
    min: int | None
    max: int | None
    # Non-empty: the only values allowed.
    choices: tuple[ParameterChoiceInfo, ...]
    # Non-empty: a host enumeration a UI may offer values from ("serial-ports").
    choices_from: str
    # Keys that may not be given with this one.
    excludes: tuple[str, ...]
    # Non-empty: a bare-name value names a bundled image of this kind.
    bundled: str
    # The value must differ among the instances on one provider.
    unique: bool
    # Non-empty: the value is the id of the instance providing this point.
    provider_of: str
    # is_list with fields: an item's string form, e.g. "{net}.{station}@{host}@{port}".
    item_format: str
    # is_list only: the parts of each item.
    fields: tuple[ParameterFieldInfo, ...]
    # "filepath"/"dirpath" only: "read" or "write" (the extension creates or
    # replaces the path); empty for other types.
    access: str


@dataclass(frozen=True)
class ParameterGroupInfo:
    """Parameters that are alternatives to one another."""

    key: str
    label: str
    description: str
    # Member parameter keys, in display order.
    parameters: tuple[str, ...]
    # How a UI names the state where no member is given (label, description),
    # or None when that state is not named.
    none: tuple[str, str] | None


@dataclass(frozen=True)
class Attachment:
    """Where an extension is attached at one of the points it attaches to."""

    # An attachment point id ("scsi", "tube", ...).
    point: str
    # The id of the instance providing the point, as the server chose it; empty
    # for one of the machine's own connectors.
    provider_id: str


@dataclass(frozen=True)
class AttachmentPointInfo:
    """A place extensions plug in, on the running machine."""

    # What attaches_to names ("tube", ...).
    id: str
    # The name a user sees ("Coprocessor", ...).
    display_name: str
    description: str
    min_occupancy: int
    # Most that may attach per provider; None when unbounded.
    max_occupancy: int | None
    # Ids of the instances providing this point; empty for the machine's own.
    provided_by: tuple[str, ...]


@dataclass(frozen=True)
class ExtensionInfo:
    """A single loaded-and-initialised extension, as the server reports it."""

    # Extension type, from the manifest (kebab-case: "aun", "rpc-serial", ...).
    name: str
    # Instance identity (a UUID, or a user-provided id).
    id: str
    # This instance's name: its label, else one composed from the manifest, else the id.
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
    # The extension's name, from its manifest.
    display_name: str
    # Where the extension is attached, one per attaches_to point, in that order.
    attachments: tuple[Attachment, ...]
    # Parameters that are alternatives to one another.
    groups: tuple[ParameterGroupInfo, ...]


def _storage_device_from_proto(proto: pe_pb2.StorageDevice) -> StorageDevice:
    return StorageDevice(
        id=proto.id,
        name=proto.name,
        kind=StorageKind(proto.kind),
        media_type=proto.media_type,
        backing_path=proto.backing_path,
        activity_indicator_name=proto.activity_indicator_name,
    )


def _choice_from_proto(proto: pe_pb2.ParameterChoiceInfo) -> ParameterChoiceInfo:
    return ParameterChoiceInfo(value=proto.value, label=proto.label, description=proto.description)


def _field_from_proto(proto: pe_pb2.ParameterFieldInfo) -> ParameterFieldInfo:
    return ParameterFieldInfo(
        key=proto.key,
        label=proto.label,
        description=proto.description,
        type=proto.type,
        required=proto.required,
        default_value=proto.default_value,
        min=proto.min if proto.HasField("min") else None,
        max=proto.max if proto.HasField("max") else None,
        choices=tuple(_choice_from_proto(c) for c in proto.choices),
    )


def _parameter_schema_from_proto(proto: pe_pb2.ParameterSchemaInfo) -> ParameterSchemaInfo:
    return ParameterSchemaInfo(
        key=proto.key,
        type=proto.type,
        description=proto.description,
        position=proto.position,
        required=proto.required,
        default_value=proto.default_value,
        label=proto.label,
        is_list=proto.is_list,
        default_from=proto.default_from,
        effective_default=proto.effective_default,
        min=proto.min if proto.HasField("min") else None,
        max=proto.max if proto.HasField("max") else None,
        choices=tuple(_choice_from_proto(c) for c in proto.choices),
        choices_from=proto.choices_from,
        excludes=tuple(proto.excludes),
        bundled=proto.bundled,
        unique=proto.unique,
        provider_of=proto.provider_of,
        item_format=proto.item_format,
        fields=tuple(_field_from_proto(f) for f in proto.fields),
        access=proto.access,
    )


def _group_from_proto(proto: pe_pb2.ParameterGroupInfo) -> ParameterGroupInfo:
    return ParameterGroupInfo(
        key=proto.key,
        label=proto.label,
        description=proto.description,
        parameters=tuple(proto.parameters),
        none=(proto.none.label, proto.none.description) if proto.HasField("none") else None,
    )


def _attachment_point_from_proto(proto: pe_pb2.AttachmentPointInfo) -> AttachmentPointInfo:
    return AttachmentPointInfo(
        id=proto.id,
        display_name=proto.display_name,
        description=proto.description,
        min_occupancy=proto.min_occupancy,
        max_occupancy=proto.max_occupancy if proto.HasField("max_occupancy") else None,
        provided_by=tuple(proto.provided_by),
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
        display_name=proto.display_name,
        attachments=tuple(Attachment(point=a.point, provider_id=a.provider_id) for a in proto.attachments),
        groups=tuple(_group_from_proto(g) for g in proto.groups),
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

    @property
    def attachment_points(self) -> list[AttachmentPointInfo]:
        """The machine's attachment points, its own then those extensions provide.

        In the server's presentation order, each with the name a user sees.
        """
        response = self._stub.ListExtensions(pe_pb2.ListExtensionsRequest())
        return [_attachment_point_from_proto(p) for p in response.attachment_points]

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

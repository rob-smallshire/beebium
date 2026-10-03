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

"""Client-side extension-adapter framework.

An *extension adapter* is a typed Python client for one Beebium server
extension: it knows the extension's logical RPC service name and messages and
exposes a friendly façade over the core's ExtensionRpc channel. Adapters are
registered as ``beebium.ext`` entry points (keyed by the kebab-case server
manifest name) and discovered via stevedore, so a third-party extension can
ship its own adapter and have it picked up with no change to the core.

This module holds the ``ExtensionAdapter`` base class and the list/describe/
create factory over the entry-point registry. Binding a *loaded* extension to
an adapter instance is done by the ``bbc.extensions`` bridge (see
``beebium.client.extensions``); the pattern mirrors sixty-north/asyoulikeit and
demonstrable-visning.
"""

from __future__ import annotations

import inspect
from abc import ABC, abstractmethod
from collections.abc import Iterator
from typing import TYPE_CHECKING, ClassVar, Self, cast

import stevedore
import stevedore.driver
import stevedore.exception

from beebium.client.exceptions import (
    ExtensionAdapterNotInstalledError,
    ExtensionAmbiguousError,
    ExtensionError,
    ExtensionNotLoadedError,
)
from beebium.client.extension_rpc import ExtensionChannel

if TYPE_CHECKING:
    from beebium.client import Beebium

# Adapters register into one of two entry-point groups, matching the server
# service that discovers them. The key of each entry point is the server
# manifest name (kebab-case: "acorn-rtc", "aun"). Peripheral extensions attach
# to an attachment point (serial-port, 1mhz-bus, ...) and are discovered via
# PeripheralExtensionService; Econet transports provide the Econet wire and are
# discovered via EconetTransportService.
PERIPHERAL_ENTRY_POINT_GROUP = "beebium.ext.peripheral"
ECONET_ENTRY_POINT_GROUP = "beebium.ext.econet"


class ExtensionAdapter(ABC):
    """Base class for a typed client adapter of a Beebium server extension.

    Subclasses set :attr:`EXTENSION_NAME` to the server manifest name they
    drive, and add typed methods that tunnel their extension's messages over
    the shared ExtensionRpc channel. Metadata (name, description, version) is
    derived from the class the way the reference frameworks do it -- the
    description is the class docstring.
    """

    #: The server manifest name (== ``beebium.ext`` entry-point key) this
    #: adapter drives. Subclasses MUST set this.
    EXTENSION_NAME: ClassVar[str] = ""

    def __init__(
        self,
        name: str,
        channel: ExtensionChannel,
        *,
        extension_id: str = "",
    ):
        """Bind the adapter to a live channel and (optionally) an instance.

        Args:
            name: The extension's manifest name (its entry-point key).
            channel: The ExtensionRpc channel that carries this extension's
                messages.
            extension_id: The server-assigned instance id to target. Empty
                routes by service name, which the server accepts only while
                exactly one loaded instance offers the service.
        """
        self._name = name
        self._channel = channel
        self._extension_id = extension_id

    @property
    def name(self) -> str:
        """The extension's manifest name."""
        return self._name

    @property
    def extension_id(self) -> str:
        """The server-assigned instance id this adapter targets (may be empty)."""
        return self._extension_id

    def _invoke_bytes(self, service: str, method: str, payload: bytes) -> bytes:
        """Unary call routed to this adapter's instance. Returns reply bytes."""
        return self._channel.invoke(service, method, payload, extension_id=self._extension_id)

    def _server_stream_bytes(self, service: str, method: str, payload: bytes) -> Iterator[bytes]:
        """Server-streaming call routed to this instance. Yields reply bytes."""
        yield from self._channel.server_stream(service, method, payload, extension_id=self._extension_id)

    @classmethod
    def describe(cls, *, single_line: bool = False) -> str:
        """A description of the adapter, taken from its class docstring."""
        doc = inspect.getdoc(cls)
        if not doc:
            return "No description available."
        doc = doc.strip()
        if single_line:
            return doc.splitlines()[0]
        return doc

    @classmethod
    def version(cls) -> str:
        """The adapter's version. Override to report a real version."""
        return "1.0.0"

    @classmethod
    @abstractmethod
    def attach(cls, bbc: Beebium) -> Self:
        """Return this adapter bound to ``bbc``, typed as the concrete class.

        The concrete-first equivalent of the category's typed subscript
        (``bbc.extensions[cls]`` or ``bbc.transport[cls]``). Each category base
        routes to the right discovery facade. Raises
        :class:`ExtensionNotLoadedError` if the server has not loaded it.
        """
        ...  # implemented per category below


class PeripheralExtensionAdapter(ExtensionAdapter):
    """Base for adapters of peripheral extensions.

    Peripheral extensions attach to an attachment point (serial-port, 1mhz-bus,
    user-port, tube, scsi, ...) and are discovered via PeripheralExtensionService.
    Reached with ``bbc.extensions[<Adapter>]`` or ``<Adapter>.attach(bbc)``.
    Registers under the ``beebium.ext.peripheral`` entry-point group.
    """

    @classmethod
    def attach(cls, bbc: Beebium) -> Self:
        return bbc.extensions[cls]


class EconetTransportAdapter(ExtensionAdapter):
    """Base for adapters of Econet transports (the Econet wire backend).

    Econet transports (AUN, Piconet) are discovered via EconetTransportService,
    whose per-instance id is also the adapter's ExtensionRpc routing id. Reached with ``bbc.transport[<Adapter>]`` or
    ``<Adapter>.attach(bbc)``. Registers under the ``beebium.ext.econet``
    entry-point group.
    """

    @classmethod
    def attach(cls, bbc: Beebium) -> Self:
        return bbc.transport[cls]


def _on_load_failure(manager, entrypoint, exception) -> None:
    """Turn a stevedore load failure into a beebium ExtensionError."""
    raise ExtensionError(
        f"Could not load the {entrypoint.name!r} adapter from the {manager.namespace!r} entry-point group: {exception}"
    ) from exception


def installed_adapter_names(group: str) -> list[str]:
    """The names of every installed adapter in ``group`` (no instantiation)."""
    manager = stevedore.ExtensionManager(
        namespace=group,
        invoke_on_load=False,
        on_load_failure_callback=_on_load_failure,
    )
    return sorted(manager.names())


def adapter_type(name: str, group: str) -> type[ExtensionAdapter]:
    """The adapter *class* registered for ``name`` in ``group`` (no instantiation).

    Raises:
        ExtensionAdapterNotInstalledError: if no adapter is registered for
            ``name`` in ``group``.
    """
    try:
        manager = stevedore.driver.DriverManager(
            namespace=group,
            name=name,
            invoke_on_load=False,
            on_load_failure_callback=_on_load_failure,
        )
    except stevedore.exception.NoMatches:
        installed = installed_adapter_names(group)
        raise ExtensionAdapterNotInstalledError(
            f"No client adapter is installed for extension {name!r} in "
            f"{group!r}. Installed: {', '.join(installed) or '(none)'}."
        ) from None
    # invoke_on_load=False, so driver is the adapter class, not an instance.
    return cast("type[ExtensionAdapter]", manager.driver)


def describe_adapter(name: str, group: str, *, single_line: bool = False) -> str:
    """Describe the adapter registered for ``name`` in ``group``, no instantiation."""
    return adapter_type(name, group).describe(single_line=single_line)


def create_adapter(name: str, group: str, channel: ExtensionChannel, extension_id: str = "") -> ExtensionAdapter:
    """Instantiate the adapter registered for ``name`` in ``group``."""
    cls = adapter_type(name, group)
    return cls(name, channel, extension_id=extension_id)


def resolve_loaded(entries, key: str, *, requested: str, kind: str, match_id: bool = True):
    """Return the single loaded entry matching ``key`` (a name or instance id).

    ``entries`` are the loaded records (objects with ``.name`` and ``.id`` --
    ``ExtensionInfo`` or ``TransportInfo``), and ``key`` is a manifest name or an
    instance id. Shared by both the peripheral and transport bridges so they
    resolve and disambiguate identically, and both bind the resolved entry's
    ``id`` as the adapter's ExtensionRpc routing id.

    An exact instance-id match wins over a name match: the server assigns the
    first instance of an extension its manifest name as its id (``"aun"``,
    then ``"aun-1"``), so with several instances loaded the first is still
    addressable by that id. Pass ``match_id=False`` when ``key`` can only be a
    manifest name (an adapter class's ``EXTENSION_NAME``), so that a class key
    stays ambiguous when several instances are loaded.

    Raises:
        ExtensionNotLoadedError: if nothing matches.
        ExtensionAmbiguousError: if several match -- i.e. a name/type key with
            more than one loaded instance; address one by its id instead.
    """
    if not key:
        raise ExtensionNotLoadedError(f"{requested} does not declare an EXTENSION_NAME.")
    if match_id:
        by_id = [e for e in entries if e.id == key]
        if len(by_id) == 1:
            return by_id[0]
    matches = [e for e in entries if e.name == key or (match_id and e.id == key)]
    if not matches:
        available = ", ".join(sorted(e.name for e in entries)) or "(none)"
        raise ExtensionNotLoadedError(
            f"{kind} {key!r} (requested via {requested}) is not loaded on the server. Loaded: {available}."
        )
    if len(matches) > 1:
        ids = ", ".join(m.id for m in matches)
        raise ExtensionAmbiguousError(
            f'{len(matches)} instances of {kind} {key!r} are loaded; address one by its id (["<id>"]): {ids}.'
        )
    return matches[0]

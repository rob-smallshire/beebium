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

"""Econet transport discovery (which transport is loaded on the server).

Wraps EconetTransportService. Use this to decide whether to drive
``bbc.transport[Aun]`` or ``bbc.transport[Piconet]`` (or any future
transport-specific service): each transport extension has a canonical name
(``aun``, ``piconet``, ...) which maps one-to-one with the corresponding
service.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import TypeVar, cast, overload

from beebium.client._proto import econet_transport_pb2, econet_transport_pb2_grpc
from beebium.client.exceptions import ExtensionError
from beebium.client.extension import (
    ECONET_ENTRY_POINT_GROUP,
    EconetTransportAdapter,
    create_adapter,
    resolve_loaded,
)
from beebium.client.extension_rpc import ExtensionChannel

T = TypeVar("T", bound=EconetTransportAdapter)


@dataclass(frozen=True)
class TransportInfo:
    """A single Econet transport extension known to the server."""

    # Canonical extension name -- "aun", "piconet", etc. Maps to the
    # transport-specific service (AunService, PiconetService) the client
    # reaches over ExtensionRpc.
    name: str

    # Human-readable description from the extension manifest.
    description: str

    # True if this transport is the one the server uses for Econet. On a BBC
    # machine every loaded transport is active, even before Econet hardware is
    # fitted or while its backend is down; Econet.status gives the link state.
    active: bool

    # Opaque, server-assigned instance id. This is the key to pass to
    # ExtensionUiService (SubscribeView / Dispatch) to drive this
    # transport's control panel, and the ``extension_id`` that routes the
    # transport's typed RPCs over ExtensionRpc. Discover it here rather
    # than hardcoding names.
    id: str

    # True if this transport implements an Extension UI, so a frontend
    # can decide whether to render an ExtensionUiService panel for it.
    has_ui: bool


class EconetTransport:
    """Discover the active Econet transport and reach its typed adapter.

    The transport counterpart of ``bbc.extensions``: it lists the loaded Econet
    transports (AUN, Piconet) and bridges them to typed adapters, keyed by name
    (generic, base type) or adapter class (concrete type).

    Usage:
        if bbc.transport.active is None:
            print("No Econet transport configured")
        elif bbc.transport.active.name == "aun":
            bbc.transport[Aun].add_peer(...)      # or Aun.attach(bbc)
        elif bbc.transport.active.name == "piconet":
            print(bbc.transport[Piconet].status.device_path)
    """

    def __init__(
        self,
        stub: econet_transport_pb2_grpc.EconetTransportServiceStub,
        channel: ExtensionChannel,
    ):
        self._stub = stub
        self._channel = channel

    def list(self) -> list[TransportInfo]:
        """List all econet transports the server knows about.

        Returns one entry per loaded transport extension. BBC machine
        variants load at most one, and report it ``active``.
        """
        request = econet_transport_pb2.ListTransportsRequest()
        response = self._stub.ListTransports(request)
        return [
            TransportInfo(
                name=t.name,
                description=t.description,
                active=t.active,
                id=t.id,
                has_ui=t.has_ui,
            )
            for t in response.transports
        ]

    @property
    def active(self) -> TransportInfo | None:
        """The single active transport, or None if none configured."""
        request = econet_transport_pb2.GetActiveTransportRequest()
        response = self._stub.GetActiveTransport(request)
        if not response.HasField("active"):
            return None
        a = response.active
        return TransportInfo(
            name=a.name,
            description=a.description,
            active=a.active,
            id=a.id,
            has_ui=a.has_ui,
        )

    # -- Typed / generic adapter access --------------------------------------

    @overload
    def __getitem__(self, key: str) -> EconetTransportAdapter: ...
    @overload
    def __getitem__(self, key: type[T]) -> T: ...

    def __getitem__(self, key: str | type[T]) -> EconetTransportAdapter | T:
        """Return a client adapter for a loaded Econet transport.

        A class key (``bbc.transport[Aun]``) returns that concrete adapter type;
        a string key resolves the installed adapter via the ``beebium.ext.econet``
        registry, typed as the base ``EconetTransportAdapter``. The string may
        be the manifest name (``"aun"``) or a specific instance id.

        Raises:
            ExtensionNotLoadedError: if that transport is not loaded.
            ExtensionAmbiguousError: if a name matches several loaded transports
                (address one by its id instead).
            ExtensionAdapterNotInstalledError: (string key) if no adapter is
                registered for the resolved transport.
        """
        # The id EconetTransportService reports is the transport's ExtensionRpc
        # routing id, so the adapter is bound to that instance.
        if isinstance(key, type):
            info = self._require_loaded(key.EXTENSION_NAME, requested=key.__name__, match_id=False)
            return key(info.name, self._channel, extension_id=info.id)
        info = self._require_loaded(key, requested=repr(key))
        return cast(
            EconetTransportAdapter,
            create_adapter(info.name, ECONET_ENTRY_POINT_GROUP, self._channel, extension_id=info.id),
        )

    @overload
    def get(self, key: str, default: None = None) -> EconetTransportAdapter | None: ...
    @overload
    def get(self, key: type[T], default: None = None) -> T | None: ...

    def get(
        self, key: str | type[T], default: EconetTransportAdapter | T | None = None
    ) -> EconetTransportAdapter | T | None:
        """Like ``self[key]`` but returns ``default`` instead of raising."""
        try:
            return self[key]
        except ExtensionError:
            return default

    def _require_loaded(self, key: str, *, requested: str, match_id: bool = True) -> TransportInfo:
        return resolve_loaded(self.list(), key, requested=requested, kind="Econet transport", match_id=match_id)

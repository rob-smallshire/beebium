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

"""Tests for the extension-adapter framework and the two typed access facades:
peripheral extensions via bbc.extensions, Econet transports via bbc.transport.

Registry tests are server-free; the bridge tests launch a real server with the
relevant extension loaded.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import (
    ExtensionAdapterNotInstalledError,
    ExtensionAmbiguousError,
    ExtensionNotLoadedError,
    ServerNotFoundError,
)
from beebium.client.extension import (
    ECONET_ENTRY_POINT_GROUP,
    PERIPHERAL_ENTRY_POINT_GROUP,
    EconetTransportAdapter,
    ExtensionAdapter,
    PeripheralExtensionAdapter,
    adapter_type,
    describe_adapter,
    installed_adapter_names,
    resolve_loaded,
)
from beebium.ext.econet.aun import Aun
from beebium.ext.econet.piconet import Piconet
from beebium.ext.peripheral.acorn_rtc import AcornRtc
from beebium.ext.peripheral.acorn_scsi import AcornScsi
from beebium.ext.peripheral.host_serial import HostSerial
from beebium.ext.peripheral.rpc_serial import RpcSerial

_PERIPHERAL = {
    "rpc-serial": RpcSerial,
    "host-serial": HostSerial,
    "acorn-rtc": AcornRtc,
    "acorn-scsi": AcornScsi,
}
_ECONET = {"aun": Aun, "piconet": Piconet}
_ALL = [(PERIPHERAL_ENTRY_POINT_GROUP, n, c) for n, c in _PERIPHERAL.items()] + [
    (ECONET_ENTRY_POINT_GROUP, n, c) for n, c in _ECONET.items()
]


# --------------------------------------------------------------------------
# Server-free registry tests
# --------------------------------------------------------------------------


def test_peripheral_group_is_registered():
    assert set(_PERIPHERAL) <= set(installed_adapter_names(PERIPHERAL_ENTRY_POINT_GROUP))


def test_econet_group_is_registered():
    assert set(_ECONET) <= set(installed_adapter_names(ECONET_ENTRY_POINT_GROUP))


@pytest.mark.parametrize("group,name,cls", _ALL)
def test_adapter_type_resolves_to_the_concrete_class(group, name, cls):
    assert adapter_type(name, group) is cls
    assert issubclass(cls, ExtensionAdapter)


@pytest.mark.parametrize("group,name,cls", _ALL)
def test_extension_name_matches_entry_point_key(group, name, cls):
    assert cls.EXTENSION_NAME == name


def test_peripheral_adapters_use_the_peripheral_base():
    for cls in _PERIPHERAL.values():
        assert issubclass(cls, PeripheralExtensionAdapter)


def test_econet_adapters_use_the_transport_base():
    for cls in _ECONET.values():
        assert issubclass(cls, EconetTransportAdapter)


def test_adapter_type_raises_for_unknown_name():
    with pytest.raises(ExtensionAdapterNotInstalledError):
        adapter_type("no-such-adapter", PERIPHERAL_ENTRY_POINT_GROUP)


def test_describe_adapter_reads_the_docstring():
    text = describe_adapter("rpc-serial", PERIPHERAL_ENTRY_POINT_GROUP, single_line=True)
    assert text and "\n" not in text
    assert len(describe_adapter("rpc-serial", PERIPHERAL_ENTRY_POINT_GROUP)) >= len(text)


# --------------------------------------------------------------------------
# resolve_loaded: the shared name-or-id resolver (server-free)
# --------------------------------------------------------------------------


@dataclass
class _Entry:
    name: str
    id: str


def test_resolve_loaded_matches_by_name():
    entry = _Entry("aun", "id-1")
    assert resolve_loaded([entry], "aun", requested="Aun", kind="transport") is entry


def test_resolve_loaded_matches_by_instance_id():
    entry = _Entry("aun", "id-1")
    assert resolve_loaded([entry], "id-1", requested="'id-1'", kind="transport") is entry


def test_resolve_loaded_raises_when_absent():
    with pytest.raises(ExtensionNotLoadedError):
        resolve_loaded([], "aun", requested="Aun", kind="transport")


def test_resolve_loaded_empty_key_raises():
    with pytest.raises(ExtensionNotLoadedError):
        resolve_loaded([_Entry("aun", "id-1")], "", requested="Aun", kind="transport")


def test_resolve_loaded_ambiguous_name_raises_but_id_disambiguates():
    entries = [_Entry("aun", "id-1"), _Entry("aun", "id-2")]
    with pytest.raises(ExtensionAmbiguousError):
        resolve_loaded(entries, "aun", requested="Aun", kind="transport")
    # The unique instance id resolves cleanly.
    assert resolve_loaded(entries, "id-2", requested="'id-2'", kind="transport").id == "id-2"


def test_resolve_loaded_exact_id_wins_over_a_shared_name():
    # The server gives the first instance its manifest name as its id, so with
    # two instances "aun" is both a name (two matches) and an id (one match).
    entries = [_Entry("aun", "aun"), _Entry("aun", "aun-1")]
    assert resolve_loaded(entries, "aun", requested="'aun'", kind="transport") is entries[0]
    assert resolve_loaded(entries, "aun-1", requested="'aun-1'", kind="transport") is entries[1]


def test_resolve_loaded_name_only_key_stays_ambiguous():
    # An adapter class key carries a manifest name, never an id.
    entries = [_Entry("aun", "aun"), _Entry("aun", "aun-1")]
    with pytest.raises(ExtensionAmbiguousError):
        resolve_loaded(entries, "aun", requested="Aun", kind="transport", match_id=False)


# --------------------------------------------------------------------------
# Integration: peripheral bridge (bbc.extensions) with rpc-serial
# --------------------------------------------------------------------------


@pytest.fixture(scope="module")
def bbc_rpc_serial(mos_filepath: Path, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=["--rpc-serial"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_extensions_subscript_by_class(bbc_rpc_serial):
    rpc = bbc_rpc_serial.extensions[RpcSerial]
    assert isinstance(rpc, RpcSerial)
    assert rpc.name == "rpc-serial"
    assert rpc.extension_id == bbc_rpc_serial.extensions.info("rpc-serial").id


def test_extensions_subscript_by_string(bbc_rpc_serial):
    rpc = bbc_rpc_serial.extensions["rpc-serial"]
    assert isinstance(rpc, RpcSerial)  # concrete class resolved from the registry


def test_extensions_subscript_by_instance_id(bbc_rpc_serial):
    info = bbc_rpc_serial.extensions.info("rpc-serial")
    rpc = bbc_rpc_serial.extensions[info.id]  # string key that is an id
    assert isinstance(rpc, RpcSerial)
    assert rpc.extension_id == info.id


def test_extensions_attach_matches_subscript(bbc_rpc_serial):
    assert RpcSerial.attach(bbc_rpc_serial).extension_id == bbc_rpc_serial.extensions[RpcSerial].extension_id


def test_bound_peripheral_adapter_drives_the_extension(bbc_rpc_serial):
    rpc = bbc_rpc_serial.extensions[RpcSerial]
    assert rpc.send(b"AB") == 2
    assert rpc.status.rx_pending >= 0


def test_extensions_unloaded_peripheral_raises(bbc_rpc_serial):
    # acorn-rtc is a registered peripheral adapter, but not loaded here.
    with pytest.raises(ExtensionNotLoadedError):
        _ = bbc_rpc_serial.extensions[AcornRtc]


def test_extensions_get_returns_none_when_unloaded(bbc_rpc_serial):
    assert bbc_rpc_serial.extensions.get(AcornRtc) is None
    assert bbc_rpc_serial.extensions.get("acorn-rtc") is None


# --------------------------------------------------------------------------
# Integration: transport bridge (bbc.transport) with AUN
# --------------------------------------------------------------------------


@pytest.fixture(scope="module")
def bbc_aun(mos_filepath: Path, basic_filepath, beebium_server_filepath):
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=["--aun", "net=1"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_transport_subscript_by_class(bbc_aun):
    aun = bbc_aun.transport[Aun]
    assert isinstance(aun, Aun)
    assert aun.name == "aun"


def test_transport_subscript_by_string(bbc_aun):
    assert isinstance(bbc_aun.transport["aun"], Aun)


def test_transport_subscript_by_instance_id(bbc_aun):
    info = next(t for t in bbc_aun.transport.list() if t.name == "aun")
    assert isinstance(bbc_aun.transport[info.id], Aun)


def test_transport_attach_matches_subscript(bbc_aun):
    assert Aun.attach(bbc_aun).extension_id == bbc_aun.transport[Aun].extension_id


def test_transport_active_is_aun(bbc_aun):
    active = bbc_aun.transport.active
    assert active is not None and active.name == "aun"


def test_transport_unloaded_raises(bbc_aun):
    # piconet is a registered transport adapter, but not the active transport.
    with pytest.raises(ExtensionNotLoadedError):
        _ = bbc_aun.transport[Piconet]
    assert bbc_aun.transport.get(Piconet) is None


def test_bound_transport_adapter_drives_the_extension(bbc_aun):
    # status round-trips through the bridge-bound adapter over ExtensionRpc,
    # proving the transport bridge reaches the extension end to end. (Mutating
    # the peer table additionally needs an active AUN backend, which depends on
    # the emulated machine bringing Econet up -- out of scope here.)
    aun = bbc_aun.transport[Aun]
    status = aun.status
    assert status.peer_count >= 0
    assert isinstance(aun.peers, list)

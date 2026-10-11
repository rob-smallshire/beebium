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

"""Tests for the peripheral-extension discovery client (bbc.extensions).

The conversion tests are server-free: they build peripheral_extension_pb2
messages directly and assert the dataclass mapping. The integration tests
launch a real server with an extension loaded and assert it is discovered.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client._proto import peripheral_extension_pb2 as pe_pb2
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.extensions import (
    Attachment,
    ExtensionInfo,
    ParameterChoiceInfo,
    ParameterSchemaInfo,
    StorageDevice,
    StorageKind,
    _attachment_point_from_proto,
    _extension_info_from_proto,
)


@pytest.fixture(scope="module")
def bbc_with_rpc_serial(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
):
    """A server launched with the rpc-serial extension loaded.

    Gives the discovery tests a known extension to find. Shared across the
    module -- discovery is read-only, so a fresh server per test is wasteful.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--rpc-serial"],
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


# --------------------------------------------------------------------------
# Server-free conversion tests
# --------------------------------------------------------------------------


def test_extension_info_from_proto_maps_all_scalar_fields():
    proto = pe_pb2.ExtensionInfo(
        name="rpc-serial",
        id="rpc-serial",
        label="RPC Serial",
        attaches_to=["serial-port"],
        provides=["serial"],
        description="Client-driven serial peer",
        has_ui=False,
    )
    info = _extension_info_from_proto(proto)

    assert isinstance(info, ExtensionInfo)
    assert info.name == "rpc-serial"
    assert info.id == "rpc-serial"
    assert info.label == "RPC Serial"
    assert info.attaches_to == ("serial-port",)
    assert info.provides == ("serial",)
    assert info.description == "Client-driven serial peer"
    assert info.has_ui is False


def test_extension_info_from_proto_maps_config_map():
    proto = pe_pb2.ExtensionInfo(name="rpc-serial", id="rpc-serial")
    proto.config["tx_buffer"] = "4096"
    proto.config["mode"] = "pty"

    info = _extension_info_from_proto(proto)

    assert info.config == {"tx_buffer": "4096", "mode": "pty"}


def test_extension_info_from_proto_maps_parameter_schema():
    proto = pe_pb2.ExtensionInfo(name="rpc-serial", id="rpc-serial")
    proto.parameters.add(
        key="tx_buffer",
        type="integer",
        description="transmit buffer size in bytes",
        position=-1,
        required=False,
        default_value="4096",
    )
    info = _extension_info_from_proto(proto)

    assert len(info.parameters) == 1
    param = info.parameters[0]
    assert isinstance(param, ParameterSchemaInfo)
    assert param.key == "tx_buffer"
    assert param.type == "integer"
    assert param.position == -1
    assert param.required is False
    assert param.default_value == "4096"


def test_extension_info_from_proto_maps_the_full_parameter_schema():
    proto = pe_pb2.ExtensionInfo(name="aun", id="aun")
    param = proto.parameters.add(
        key="map",
        type="string",
        label="Peers",
        is_list=True,
        default_from="",
        effective_default="",
        choices_from="",
        item_format="{net}.{station}@{host}@{port}",
    )
    param.fields.add(key="station", label="Station", type="integer", required=True, min=1, max=254)
    param.fields.add(key="host", label="Host", type="string")
    port = proto.parameters.add(key="port", type="integer", min=0, max=65535)
    port.choices.add(value="auto", label="Automatic", description="Any free port.")
    map_file = proto.parameters.add(key="map-file", type="filepath", default_from="aun-map-file")
    map_file.effective_default = "/home/user/aun-map.json"
    group = proto.groups.add(key="start", label="Start", parameters=["port", "map"])
    group.none.label = "Unchanged"
    proto.groups.add(key="other", label="Other")

    info = _extension_info_from_proto(proto)

    peers, port_info, map_file_info = info.parameters
    assert peers.label == "Peers"
    assert peers.is_list is True
    assert peers.item_format == "{net}.{station}@{host}@{port}"
    assert [f.key for f in peers.fields] == ["station", "host"]
    assert peers.fields[0].min == 1
    assert peers.fields[0].max == 254
    assert peers.fields[1].min is None
    assert peers.min is None
    assert port_info.min == 0
    assert port_info.max == 65535
    assert port_info.choices == (ParameterChoiceInfo("auto", "Automatic", "Any free port."),)
    assert map_file_info.default_value == ""
    assert map_file_info.effective_default == "/home/user/aun-map.json"
    assert info.groups[0].parameters == ("port", "map")
    assert info.groups[0].none == ("Unchanged", "")
    assert info.groups[1].none is None


def test_extension_info_from_proto_maps_attachments_and_display_name():
    proto = pe_pb2.ExtensionInfo(name="scsi-hard-disc", id="hd0", display_name="Hard Disc")
    proto.attachments.add(point="scsi", provider_id="acorn-scsi")

    info = _extension_info_from_proto(proto)

    assert info.display_name == "Hard Disc"
    assert info.attachments == (Attachment(point="scsi", provider_id="acorn-scsi"),)


def test_attachment_point_from_proto_distinguishes_unbounded_occupancy():
    bounded = _attachment_point_from_proto(
        pe_pb2.AttachmentPointInfo(id="tube", display_name="Coprocessor", max_occupancy=1)
    )
    unbounded = _attachment_point_from_proto(pe_pb2.AttachmentPointInfo(id="1mhz-bus"))

    assert bounded.max_occupancy == 1
    assert unbounded.max_occupancy is None


def test_extension_info_from_proto_maps_storage_devices():
    proto = pe_pb2.ExtensionInfo(name="acorn-scsi", id="acorn-scsi")
    proto.storage_devices.add(
        id="acorn-scsi",
        name="Hard Disc Drive 0",
        kind=pe_pb2.StorageDevice.FIXED,
        media_type="hard-disc",
        backing_path="/tmp/hd0.dat",
        activity_indicator_name="hdd-0-activity-led",
    )
    info = _extension_info_from_proto(proto)

    assert len(info.storage_devices) == 1
    device = info.storage_devices[0]
    assert isinstance(device, StorageDevice)
    assert device.id == "acorn-scsi"
    assert device.name == "Hard Disc Drive 0"
    assert device.kind is StorageKind.FIXED
    assert device.media_type == "hard-disc"
    assert device.backing_path == "/tmp/hd0.dat"
    assert device.activity_indicator_name == "hdd-0-activity-led"


def test_extension_info_dataclass_is_frozen():
    proto = pe_pb2.ExtensionInfo(name="rpc-serial", id="rpc-serial")
    info = _extension_info_from_proto(proto)
    with pytest.raises(AttributeError):  # frozen dataclass -> FrozenInstanceError
        info.name = "changed"  # type: ignore[misc]


# --------------------------------------------------------------------------
# Integration tests (launch a real server with an extension loaded)
# --------------------------------------------------------------------------


def test_loaded_reports_a_launched_extension(bbc_with_rpc_serial):
    loaded = bbc_with_rpc_serial.extensions.loaded
    names = {info.name for info in loaded}
    assert "rpc-serial" in names, f"rpc-serial not among loaded extensions: {names}"


def test_info_looks_up_by_name(bbc_with_rpc_serial):
    info = bbc_with_rpc_serial.extensions.info("rpc-serial")
    assert info is not None
    assert info.name == "rpc-serial"
    assert "serial-port" in info.attaches_to


def test_info_returns_none_for_unknown_extension(bbc_with_rpc_serial):
    assert bbc_with_rpc_serial.extensions.info("no-such-extension") is None


def test_extensions_membership_and_iteration(bbc_with_rpc_serial):
    exts = bbc_with_rpc_serial.extensions
    assert "rpc-serial" in exts
    assert len(exts) >= 1
    assert any(info.name == "rpc-serial" for info in exts)


def test_attachment_points_are_named_by_the_server(bbc_with_rpc_serial):
    points = {p.id: p for p in bbc_with_rpc_serial.extensions.attachment_points}
    assert points["tube"].display_name == "Coprocessor"
    assert points["tube"].description == "The Tube connector, which takes a second processor."
    assert points["tube"].max_occupancy == 1
    assert points["serial-port"].display_name == "Serial Port"
    assert points["1mhz-bus"].max_occupancy is None
    assert points["serial-port"].provided_by == ()


def test_a_launched_extension_says_where_it_is_attached(bbc_with_rpc_serial):
    info = bbc_with_rpc_serial.extensions.info("rpc-serial")
    assert info is not None
    assert info.display_name
    assert info.attachments == (Attachment(point="serial-port", provider_id=""),)

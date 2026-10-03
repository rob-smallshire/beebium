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

"""Unit tests for the EconetTransport client wrapper."""

from __future__ import annotations

from unittest.mock import MagicMock

import pytest

from beebium.client.econet_transport import EconetTransport, TransportInfo


class MockTransport:
    def __init__(self, name, description, active, id="", has_ui=False):
        self.name = name
        self.description = description
        self.active = active
        self.id = id
        self.has_ui = has_ui


class MockListResponse:
    def __init__(self, transports=None):
        self.transports = transports if transports is not None else []


class MockGetActiveResponse:
    def __init__(self, active=None):
        self._active = active

    def HasField(self, name):  # noqa: N802 (proto API)
        return name == "active" and self._active is not None

    @property
    def active(self):
        return self._active


@pytest.fixture
def mock_stub():
    stub = MagicMock()
    stub.ListTransports.return_value = MockListResponse()
    stub.GetActiveTransport.return_value = MockGetActiveResponse()
    return stub


@pytest.fixture
def mock_channel():
    # EconetTransport keeps the channel only to hand to the typed adapters it
    # bridges to. The cases here cover list()/active, which never reach it.
    return MagicMock()


@pytest.fixture
def transport(mock_stub, mock_channel):
    return EconetTransport(mock_stub, mock_channel)


def test_list_empty(transport):
    assert transport.list() == []


def test_list_returns_active_aun(mock_stub, transport):
    mock_stub.ListTransports.return_value = MockListResponse(
        transports=[MockTransport("aun", "AUN UDP transport", True)]
    )
    transports = transport.list()
    assert len(transports) == 1
    assert isinstance(transports[0], TransportInfo)
    assert transports[0].name == "aun"
    assert transports[0].active is True


def test_list_surfaces_id_and_has_ui(mock_stub, transport):
    # The instance id and has_ui flag are what a frontend needs to drive
    # the transport's ExtensionUiService panel; they must flow through.
    mock_stub.ListTransports.return_value = MockListResponse(
        transports=[
            MockTransport(
                "aun",
                "AUN UDP transport",
                True,
                id="42eba4e4-bcd5-4362-b8f5-6c7b44d333fc",
                has_ui=True,
            )
        ]
    )
    info = transport.list()[0]
    assert info.id == "42eba4e4-bcd5-4362-b8f5-6c7b44d333fc"
    assert info.has_ui is True


def test_active_none_when_unset(transport):
    assert transport.active is None


def test_active_returns_aun(mock_stub, transport):
    mock_stub.GetActiveTransport.return_value = MockGetActiveResponse(
        active=MockTransport("aun", "AUN UDP transport", True)
    )
    active = transport.active
    assert active is not None
    assert active.name == "aun"
    assert active.active is True


def test_active_returns_piconet(mock_stub, transport):
    mock_stub.GetActiveTransport.return_value = MockGetActiveResponse(
        active=MockTransport("piconet", "Piconet USB-CDC bridge", True)
    )
    active = transport.active
    assert active is not None
    assert active.name == "piconet"


# -- ExtensionRpc routing: the adapter targets the transport's instance id ----


def _aun_status_channel():
    from beebium.ext.econet.aun._proto import aun_pb2

    channel = MagicMock()
    channel.invoke.return_value = aun_pb2.AunGetStatusResponse(
        connected=True, local_port=32768, peer_count=0
    ).SerializeToString()
    return channel


def test_class_subscript_binds_the_transport_instance_id(mock_stub):
    from beebium.ext.econet.aun import Aun

    mock_stub.ListTransports.return_value = MockListResponse(
        transports=[MockTransport("aun", "AUN UDP transport", True, id="aun-instance")]
    )
    channel = _aun_status_channel()
    aun = EconetTransport(mock_stub, channel)[Aun]
    assert aun.extension_id == "aun-instance"

    _ = aun.status
    assert channel.invoke.call_args.kwargs["extension_id"] == "aun-instance"


def test_string_subscript_binds_the_transport_instance_id(mock_stub):
    mock_stub.ListTransports.return_value = MockListResponse(
        transports=[MockTransport("aun", "AUN UDP transport", True, id="aun-instance")]
    )
    channel = _aun_status_channel()
    aun = EconetTransport(mock_stub, channel)["aun"]
    assert aun.extension_id == "aun-instance"

    _ = aun.status
    assert channel.invoke.call_args.kwargs["extension_id"] == "aun-instance"


def test_piconet_adapter_binds_the_transport_instance_id(mock_stub):
    from beebium.ext.econet.piconet import Piconet
    from beebium.ext.econet.piconet._proto import piconet_service_pb2

    mock_stub.ListTransports.return_value = MockListResponse(
        transports=[MockTransport("piconet", "Piconet USB-CDC bridge", True, id="piconet-instance")]
    )
    channel = MagicMock()
    channel.invoke.return_value = piconet_service_pb2.PiconetGetStatusResponse(
        device_path="/dev/tty.usbmodem101", serial_open=True
    ).SerializeToString()
    piconet = EconetTransport(mock_stub, channel)[Piconet]
    assert piconet.extension_id == "piconet-instance"

    _ = piconet.status
    assert channel.invoke.call_args.kwargs["extension_id"] == "piconet-instance"


def test_two_transports_of_one_kind_are_addressed_by_id(mock_stub):
    from beebium.client.exceptions import ExtensionAmbiguousError
    from beebium.ext.econet.aun import Aun

    mock_stub.ListTransports.return_value = MockListResponse(
        transports=[
            MockTransport("aun", "AUN UDP transport", True, id="aun"),
            MockTransport("aun", "AUN UDP transport", True, id="aun-1"),
        ]
    )
    channel = _aun_status_channel()
    transport = EconetTransport(mock_stub, channel)

    # Each instance is reachable by its id, and the adapter routes to it.
    first = transport["aun"]
    assert first.extension_id == "aun"
    second = transport["aun-1"]
    assert isinstance(second, Aun)
    assert second.extension_id == "aun-1"
    _ = second.status
    assert channel.invoke.call_args.kwargs["extension_id"] == "aun-1"

    # A class key cannot choose between two instances of the same kind.
    with pytest.raises(ExtensionAmbiguousError, match="aun-1"):
        _ = transport[Aun]

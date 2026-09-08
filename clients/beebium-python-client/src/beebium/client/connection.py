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

"""gRPC connection management for the beebium client."""

from __future__ import annotations

import grpc

from beebium.client._proto import (
    audio_pb2_grpc,
    debugger_pb2_grpc,
    disc_pb2_grpc,
    econet_pb2_grpc,
    econet_transport_pb2_grpc,
    extension_rpc_pb2_grpc,
    extension_ui_pb2_grpc,
    indicator_pb2_grpc,
    keyboard_pb2_grpc,
    peripheral_extension_pb2_grpc,
    serial_pb2_grpc,
    sideways_pb2_grpc,
    system_pb2_grpc,
    tube_pb2_grpc,
    video_pb2_grpc,
)
from beebium.client.exceptions import ConnectionError


class Connection:
    """Manages a gRPC connection to a beebium server.

    Provides access to the service stubs for all beebium gRPC services.
    """

    def __init__(self, target: str, timeout: float = 5.0):
        """Create a connection to a beebium server.

        Args:
            target: The gRPC target string (e.g., "localhost:48875").
            timeout: Connection timeout in seconds.

        Raises:
            ConnectionError: If the connection cannot be established.
        """
        self._target = target
        self._channel: grpc.Channel | None = None
        self._debugger_stub: debugger_pb2_grpc.DebuggerControlStub | None = None
        self._coprocessor_debugger_stub: debugger_pb2_grpc.CoprocessorDebuggerControlStub | None = None
        self._device_inspection_stub: debugger_pb2_grpc.DeviceInspectionStub | None = None
        self._disc_stub: disc_pb2_grpc.DiscServiceStub | None = None
        self._econet_stub: econet_pb2_grpc.EconetServiceStub | None = None
        self._serial_stub: serial_pb2_grpc.SerialServiceStub | None = None
        self._extension_rpc_stub: extension_rpc_pb2_grpc.ExtensionRpcStub | None = None
        self._econet_transport_stub: econet_transport_pb2_grpc.EconetTransportServiceStub | None = None
        self._extension_ui_stub: extension_ui_pb2_grpc.ExtensionUiServiceStub | None = None
        self._indicator_stub: indicator_pb2_grpc.IndicatorServiceStub | None = None
        self._keyboard_stub: keyboard_pb2_grpc.KeyboardServiceStub | None = None
        self._sideways_stub: sideways_pb2_grpc.SidewaysServiceStub | None = None
        self._system_stub: system_pb2_grpc.SystemServiceStub | None = None
        self._tube_stub: tube_pb2_grpc.TubeServiceStub | None = None
        self._video_stub: video_pb2_grpc.VideoServiceStub | None = None
        self._audio_stub: audio_pb2_grpc.AudioServiceStub | None = None
        self._peripheral_extension_stub: peripheral_extension_pb2_grpc.PeripheralExtensionServiceStub | None = None

        self._connect(timeout)

    def _connect(self, timeout: float) -> None:
        """Establish the gRPC connection."""
        self._channel = grpc.insecure_channel(self._target)

        # Wait for the channel to be ready
        try:
            grpc.channel_ready_future(self._channel).result(timeout=timeout)
        except grpc.FutureTimeoutError:
            self._channel.close()
            self._channel = None
            raise ConnectionError(
                f"Failed to connect to beebium server at {self._target} within {timeout} seconds"
            ) from None

        # Create service stubs
        self._debugger_stub = debugger_pb2_grpc.DebuggerControlStub(self._channel)
        self._coprocessor_debugger_stub = debugger_pb2_grpc.CoprocessorDebuggerControlStub(self._channel)
        self._device_inspection_stub = debugger_pb2_grpc.DeviceInspectionStub(self._channel)
        self._disc_stub = disc_pb2_grpc.DiscServiceStub(self._channel)
        self._econet_stub = econet_pb2_grpc.EconetServiceStub(self._channel)
        self._serial_stub = serial_pb2_grpc.SerialServiceStub(self._channel)
        self._extension_rpc_stub = extension_rpc_pb2_grpc.ExtensionRpcStub(self._channel)
        self._econet_transport_stub = econet_transport_pb2_grpc.EconetTransportServiceStub(self._channel)
        self._extension_ui_stub = extension_ui_pb2_grpc.ExtensionUiServiceStub(self._channel)
        self._indicator_stub = indicator_pb2_grpc.IndicatorServiceStub(self._channel)
        self._keyboard_stub = keyboard_pb2_grpc.KeyboardServiceStub(self._channel)
        self._sideways_stub = sideways_pb2_grpc.SidewaysServiceStub(self._channel)
        self._system_stub = system_pb2_grpc.SystemServiceStub(self._channel)
        self._tube_stub = tube_pb2_grpc.TubeServiceStub(self._channel)
        self._video_stub = video_pb2_grpc.VideoServiceStub(self._channel)
        self._audio_stub = audio_pb2_grpc.AudioServiceStub(self._channel)
        self._peripheral_extension_stub = peripheral_extension_pb2_grpc.PeripheralExtensionServiceStub(self._channel)

    @property
    def target(self) -> str:
        """The gRPC target string."""
        return self._target

    @property
    def is_connected(self) -> bool:
        """True if the connection is established."""
        return self._channel is not None

    @property
    def debugger_stub(self) -> debugger_pb2_grpc.DebuggerControlStub:
        """The DebuggerControl service stub."""
        if self._debugger_stub is None:
            raise ConnectionError("Not connected")
        return self._debugger_stub

    @property
    def device_inspection_stub(self) -> debugger_pb2_grpc.DeviceInspectionStub:
        """The DeviceInspection service stub."""
        if self._device_inspection_stub is None:
            raise ConnectionError("Not connected")
        return self._device_inspection_stub

    @property
    def keyboard_stub(self) -> keyboard_pb2_grpc.KeyboardServiceStub:
        """The KeyboardService stub."""
        if self._keyboard_stub is None:
            raise ConnectionError("Not connected")
        return self._keyboard_stub

    @property
    def video_stub(self) -> video_pb2_grpc.VideoServiceStub:
        """The VideoService stub."""
        if self._video_stub is None:
            raise ConnectionError("Not connected")
        return self._video_stub

    @property
    def system_stub(self) -> system_pb2_grpc.SystemServiceStub:
        """The SystemService stub."""
        if self._system_stub is None:
            raise ConnectionError("Not connected")
        return self._system_stub

    @property
    def disc_stub(self) -> disc_pb2_grpc.DiscServiceStub:
        """The DiscService stub."""
        if self._disc_stub is None:
            raise ConnectionError("Not connected")
        return self._disc_stub

    @property
    def econet_stub(self) -> econet_pb2_grpc.EconetServiceStub:
        """The EconetService stub."""
        if self._econet_stub is None:
            raise ConnectionError("Not connected")
        return self._econet_stub

    @property
    def serial_stub(self) -> serial_pb2_grpc.SerialServiceStub:
        """The SerialService stub."""
        if self._serial_stub is None:
            raise ConnectionError("Not connected")
        return self._serial_stub

    @property
    def extension_rpc_stub(self) -> extension_rpc_pb2_grpc.ExtensionRpcStub:
        """The ExtensionRpc stub: the one channel for extension-provided APIs."""
        if self._extension_rpc_stub is None:
            raise ConnectionError("Not connected")
        return self._extension_rpc_stub

    @property
    def econet_transport_stub(self) -> econet_transport_pb2_grpc.EconetTransportServiceStub:
        """The EconetTransportService stub (transport discovery)."""
        if self._econet_transport_stub is None:
            raise ConnectionError("Not connected")
        return self._econet_transport_stub

    @property
    def extension_ui_stub(self) -> extension_ui_pb2_grpc.ExtensionUiServiceStub:
        """The ExtensionUiService stub (server-driven UI for extensions)."""
        if self._extension_ui_stub is None:
            raise ConnectionError("Not connected")
        return self._extension_ui_stub

    @property
    def indicator_stub(self) -> indicator_pb2_grpc.IndicatorServiceStub:
        """The IndicatorService stub (LED and motor activity state)."""
        if self._indicator_stub is None:
            raise ConnectionError("Not connected")
        return self._indicator_stub

    @property
    def sideways_stub(self) -> sideways_pb2_grpc.SidewaysServiceStub:
        """The SidewaysService stub (slot topology, configuration, events)."""
        if self._sideways_stub is None:
            raise ConnectionError("Not connected")
        return self._sideways_stub

    @property
    def coprocessor_debugger_stub(self) -> debugger_pb2_grpc.CoprocessorDebuggerControlStub:
        """The CoprocessorDebuggerControl service stub."""
        if self._coprocessor_debugger_stub is None:
            raise ConnectionError("Not connected")
        return self._coprocessor_debugger_stub

    @property
    def tube_stub(self) -> tube_pb2_grpc.TubeServiceStub:
        """The TubeService stub."""
        if self._tube_stub is None:
            raise ConnectionError("Not connected")
        return self._tube_stub

    @property
    def audio_stub(self) -> audio_pb2_grpc.AudioServiceStub:
        """The AudioService stub (sample streaming and format)."""
        if self._audio_stub is None:
            raise ConnectionError("Not connected")
        return self._audio_stub

    @property
    def peripheral_extension_stub(
        self,
    ) -> peripheral_extension_pb2_grpc.PeripheralExtensionServiceStub:
        """The PeripheralExtensionService stub (loaded-extension discovery)."""
        if self._peripheral_extension_stub is None:
            raise ConnectionError("Not connected")
        return self._peripheral_extension_stub

    def close(self) -> None:
        """Close the connection."""
        if self._channel is not None:
            self._channel.close()
            self._channel = None
            self._debugger_stub = None
            self._coprocessor_debugger_stub = None
            self._device_inspection_stub = None
            self._disc_stub = None
            self._econet_stub = None
            self._econet_transport_stub = None
            self._extension_ui_stub = None
            self._indicator_stub = None
            self._keyboard_stub = None
            self._sideways_stub = None
            self._system_stub = None
            self._tube_stub = None
            self._video_stub = None
            self._audio_stub = None
            self._peripheral_extension_stub = None

    def __enter__(self) -> Connection:
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

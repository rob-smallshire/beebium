# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

from beebium._proto import debugger_pb2_grpc, keyboard_pb2_grpc, video_pb2_grpc
from beebium.exceptions import ConnectionError


class Connection:
    """Manages a gRPC connection to a beebium server.

    Provides access to the service stubs for all beebium gRPC services.
    """

    def __init__(self, target: str, timeout: float = 5.0):
        """Create a connection to a beebium server.

        Args:
            target: The gRPC target string (e.g., "localhost:50051").
            timeout: Connection timeout in seconds.

        Raises:
            ConnectionError: If the connection cannot be established.
        """
        self._target = target
        self._channel: grpc.Channel | None = None
        self._debugger_stub: debugger_pb2_grpc.DebuggerControlStub | None = None
        self._cpu_stub: debugger_pb2_grpc.Debugger6502Stub | None = None
        self._keyboard_stub: keyboard_pb2_grpc.KeyboardServiceStub | None = None
        self._video_stub: video_pb2_grpc.VideoServiceStub | None = None

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
                f"Failed to connect to beebium server at {self._target} "
                f"within {timeout} seconds"
            ) from None

        # Create service stubs
        self._debugger_stub = debugger_pb2_grpc.DebuggerControlStub(self._channel)
        self._cpu_stub = debugger_pb2_grpc.Debugger6502Stub(self._channel)
        self._keyboard_stub = keyboard_pb2_grpc.KeyboardServiceStub(self._channel)
        self._video_stub = video_pb2_grpc.VideoServiceStub(self._channel)

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
    def cpu_stub(self) -> debugger_pb2_grpc.Debugger6502Stub:
        """The Debugger6502 service stub."""
        if self._cpu_stub is None:
            raise ConnectionError("Not connected")
        return self._cpu_stub

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

    def close(self) -> None:
        """Close the connection."""
        if self._channel is not None:
            self._channel.close()
            self._channel = None
            self._debugger_stub = None
            self._cpu_stub = None
            self._keyboard_stub = None
            self._video_stub = None

    def __enter__(self) -> Connection:
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

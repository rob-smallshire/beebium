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

"""Main Beebium client class."""

from __future__ import annotations

import contextlib
from collections.abc import Iterator
from pathlib import Path

import beebium
from beebium.basic import Basic
from beebium.connection import Connection
from beebium.cpu import CPU
from beebium.debugger import Debugger
from beebium.keyboard import Keyboard
from beebium.memory import Memory
from beebium.server import ServerProcess
from beebium.video import Video


class Beebium:
    """Main client for interacting with a beebium emulator instance.

    Can either connect to an existing server or manage its own server process.

    Usage:
        # Connect to existing server
        with Beebium.connect() as bbc:
            bbc.debugger.stop()
            bbc.memory[0x1000] = 0x42

        # Start and manage server
        with Beebium.launch(mos_filepath="/path/to/acorn-mos_1_20.rom") as bbc:
            bbc.keyboard.type("PRINT 42")
            bbc.keyboard.press_return()
    """

    def __init__(
        self,
        connection: Connection,
        server: ServerProcess | None = None,
    ):
        """Create a Beebium client.

        Use the class methods connect() or launch() instead of this constructor.

        Args:
            connection: The gRPC connection to the server.
            server: Optional server process being managed.
        """
        self._connection = connection
        self._server = server
        self._debugger: Debugger | None = None
        self._cpu: CPU | None = None
        self._keyboard: Keyboard | None = None
        self._video: Video | None = None
        self._memory: Memory | None = None
        self._basic: Basic | None = None

    @classmethod
    def connect(cls, target: str | None = None, timeout: float = 5.0) -> Beebium:
        """Connect to an already-running beebium-server.

        Args:
            target: The gRPC target string (e.g., "localhost:48875").
                   Defaults to localhost on DEFAULT_GRPC_PORT (48875).
            timeout: Connection timeout in seconds.

        Returns:
            A connected Beebium client.

        Raises:
            ConnectionError: If the connection cannot be established.
        """
        if target is None:
            target = f"localhost:{beebium.DEFAULT_GRPC_PORT}"
        connection = Connection(target, timeout=timeout)
        return cls(connection)

    @classmethod
    @contextlib.contextmanager
    def launch(
        cls,
        mos_filepath: str | Path,
        basic_filepath: str | Path | None = None,
        server_filepath: str | Path | None = None,
        port: int = 0,
        startup_timeout: float = 10.0,
        connection_timeout: float = 5.0,
    ) -> Iterator[Beebium]:
        """Start a beebium-server process and connect to it.

        The server is automatically stopped when the context manager exits.

        Args:
            mos_filepath: Path to the MOS ROM file (required).
            basic_filepath: Path to the BASIC ROM file (optional).
            server_filepath: Path to the beebium-server executable (optional).
            port: Port to listen on. If 0 (default), a free port is allocated.
            startup_timeout: Maximum time to wait for server to start (seconds).
            connection_timeout: Maximum time to wait for connection (seconds).

        Yields:
            A connected Beebium client.

        Raises:
            ServerStartupError: If the server fails to start.
            ConnectionError: If the connection cannot be established.
        """
        server = ServerProcess(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            port=port,
        )

        try:
            server.start(timeout=startup_timeout)
            connection = Connection(server.target, timeout=connection_timeout)
            client = cls(connection, server=server)
            yield client
        finally:
            server.stop()

    @property
    def target(self) -> str:
        """The gRPC target string."""
        return self._connection.target

    @property
    def debugger(self) -> Debugger:
        """Access debugger control (execution, breakpoints)."""
        if self._debugger is None:
            self._debugger = Debugger(self._connection.debugger_stub)
        return self._debugger

    @property
    def cpu(self) -> CPU:
        """Access 6502 CPU registers."""
        if self._cpu is None:
            self._cpu = CPU(self._connection.cpu_stub)
        return self._cpu

    @property
    def keyboard(self) -> Keyboard:
        """Access keyboard input."""
        if self._keyboard is None:
            self._keyboard = Keyboard(self._connection.keyboard_stub)
        return self._keyboard

    @property
    def video(self) -> Video:
        """Access video frame streaming."""
        if self._video is None:
            self._video = Video(self._connection.video_stub)
        return self._video

    @property
    def memory(self) -> Memory:
        """Access memory read/write with subscript notation."""
        if self._memory is None:
            self._memory = Memory(self._connection.debugger_stub)
        return self._memory

    @property
    def basic(self) -> Basic:
        """Access BBC BASIC workflow helpers."""
        if self._basic is None:
            self._basic = Basic(self)
        return self._basic

    def close(self) -> None:
        """Close the connection and stop any managed server."""
        self._connection.close()
        if self._server is not None:
            self._server.stop()
            self._server = None

    def __enter__(self) -> Beebium:
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

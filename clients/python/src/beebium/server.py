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

"""Server process management for the beebium client."""

from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import time
from pathlib import Path

import grpc

from beebium.exceptions import ServerNotFoundError, ServerStartupError


class ServerProcess:
    """Manages a beebium-server subprocess.

    Handles startup, port allocation, health checking, and graceful shutdown.

    Usage:
        with ServerProcess(mos_filepath="OS12.ROM") as server:
            # server is now running
            print(f"Server running on port {server.port}")
            # ... do work ...
        # server is automatically stopped
    """

    def __init__(
        self,
        mos_filepath: str | Path,
        basic_filepath: str | Path | None = None,
        server_filepath: str | Path | None = None,
        port: int = 0,
    ):
        """Create a server process manager.

        Args:
            mos_filepath: Path to the MOS ROM file (required).
            basic_filepath: Path to the BASIC ROM file (optional).
            server_filepath: Path to the beebium-server executable (optional).
                If not provided, searches BEEBIUM_SERVER env var, PATH, and
                common build locations.
            port: Port to listen on. If 0 (default), a free port is allocated.
        """
        self._mos_filepath = Path(mos_filepath)
        self._basic_filepath = Path(basic_filepath) if basic_filepath else None
        self._server_filepath = self._find_server(server_filepath)
        self._port = port if port != 0 else self._find_free_port()
        self._process: subprocess.Popen[bytes] | None = None

    @property
    def port(self) -> int:
        """The port the server is listening on."""
        return self._port

    @property
    def target(self) -> str:
        """gRPC target string (host:port)."""
        return f"localhost:{self._port}"

    @property
    def is_running(self) -> bool:
        """True if the server process is still running."""
        if self._process is None:
            return False
        return self._process.poll() is None

    def start(self, timeout: float = 10.0) -> None:
        """Start the server and wait for it to be ready.

        Args:
            timeout: Maximum time to wait for the server to start (seconds).

        Raises:
            ServerStartupError: If the server fails to start within timeout.
        """
        if self._process is not None:
            raise ServerStartupError("Server is already running")

        # Validate ROM files exist
        if not self._mos_filepath.exists():
            raise ServerStartupError(f"MOS ROM not found: {self._mos_filepath}")
        if self._basic_filepath and not self._basic_filepath.exists():
            raise ServerStartupError(f"BASIC ROM not found: {self._basic_filepath}")

        # Build command line
        cmd = [
            str(self._server_filepath),
            "--mos",
            str(self._mos_filepath),
            "--port",
            str(self._port),
        ]
        if self._basic_filepath:
            cmd.extend(["--basic", str(self._basic_filepath)])

        # Start the server process
        self._process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        # Wait for the server to be ready
        if not self._wait_for_ready(timeout):
            self.stop(timeout=1.0)
            raise ServerStartupError(
                f"Server failed to start within {timeout} seconds"
            )

    def stop(self, timeout: float = 5.0) -> None:
        """Stop the server gracefully, then forcefully if needed.

        Args:
            timeout: Maximum time to wait for graceful shutdown (seconds).
        """
        if self._process is None:
            return

        # Try graceful shutdown first (SIGTERM)
        self._process.terminate()

        try:
            self._process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            # Force kill if graceful shutdown didn't work
            self._process.kill()
            self._process.wait(timeout=1.0)

        self._process = None

    def _wait_for_ready(self, timeout: float) -> bool:
        """Wait for the server to be ready to accept connections."""
        deadline = time.monotonic() + timeout
        poll_interval = 0.1

        while time.monotonic() < deadline:
            # Check if process has died
            if self._process is not None and self._process.poll() is not None:
                return False

            # Try to connect
            try:
                channel = grpc.insecure_channel(self.target)
                grpc.channel_ready_future(channel).result(timeout=poll_interval)
                channel.close()
                return True
            except grpc.FutureTimeoutError:
                pass
            except Exception:
                time.sleep(poll_interval)

        return False

    def _find_server(self, path: str | Path | None) -> Path:
        """Find the beebium-server executable.

        Search order:
        1. Explicit path argument
        2. BEEBIUM_SERVER environment variable
        3. PATH lookup for 'beebium-server'
        4. Common build locations relative to this package
        """
        # 1. Explicit path
        if path is not None:
            explicit = Path(path)
            if explicit.exists() and os.access(explicit, os.X_OK):
                return explicit
            raise ServerNotFoundError(f"Server not found at specified path: {path}")

        # 2. Environment variable
        env_path = os.environ.get("BEEBIUM_SERVER")
        if env_path:
            env_server = Path(env_path)
            if env_server.exists() and os.access(env_server, os.X_OK):
                return env_server
            raise ServerNotFoundError(
                f"BEEBIUM_SERVER points to invalid path: {env_path}"
            )

        # 3. PATH lookup
        which_result = shutil.which("beebium-server")
        if which_result:
            return Path(which_result)

        # 4. Common build locations
        # Try to find relative to the Python package (assumes in-repo development)
        package_dirpath = Path(__file__).parent
        candidates = [
            # clients/python/src/beebium -> build/src/server
            package_dirpath.parent.parent.parent.parent / "build" / "src" / "server" / "beebium-server",
            # clients/python/src/beebium -> cmake-build-debug/src/server
            package_dirpath.parent.parent.parent.parent / "cmake-build-debug" / "src" / "server" / "beebium-server",
            # clients/python/src/beebium -> cmake-build-release/src/server
            package_dirpath.parent.parent.parent.parent / "cmake-build-release" / "src" / "server" / "beebium-server",
        ]

        for candidate in candidates:
            if candidate.exists() and os.access(candidate, os.X_OK):
                return candidate

        raise ServerNotFoundError(
            "beebium-server not found. Set BEEBIUM_SERVER environment variable "
            "or add beebium-server to PATH."
        )

    @staticmethod
    def _find_free_port() -> int:
        """Find an available TCP port."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("", 0))
            s.listen(1)
            return s.getsockname()[1]

    def __enter__(self) -> ServerProcess:
        self.start()
        return self

    def __exit__(self, *args: object) -> None:
        self.stop()

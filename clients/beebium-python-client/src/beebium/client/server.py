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

"""Server process management for the beebium client."""

from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

import grpc

import beebium.client as beebium
from beebium.client.exceptions import ServerNotFoundError, ServerStartupError


class ServerProcess:
    """Manages a beebium-server subprocess.

    Handles startup, port allocation, health checking, and graceful shutdown.

    Usage:
        with ServerProcess(mos_filepath="acorn-mos_1_20.rom") as server:
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
        extra_args: list[str] | None = None,
    ):
        """Create a server process manager.

        Args:
            mos_filepath: Path to the MOS ROM file (required).
            basic_filepath: Path to the BASIC ROM file (optional).
            server_filepath: Path to the beebium-server executable (optional).
                If not provided, searches BEEBIUM_SERVER env var, PATH, and
                common build locations.
            port: Port to listen on. If 0 (default), a free port is allocated.
            extra_args: Additional command-line arguments to pass to the server
                (e.g., ["--tube", "65C02-3MHz"]).
        """
        self._mos_filepath = Path(mos_filepath)
        self._basic_filepath = Path(basic_filepath) if basic_filepath else None
        self._server_filepath = self._find_server(server_filepath)
        self._port = port if port != 0 else self._find_free_port()
        self._extra_args = extra_args or []
        self._process: subprocess.Popen[bytes] | None = None
        self._provenance_instance_uuid = str(uuid.uuid4())
        # Background reader threads continuously drain the server's stdout and
        # stderr pipes into these buffers. Draining serves two purposes: it
        # prevents the server from blocking on a full pipe during a long
        # session (the OS pipe buffer is only ~64KB), and it preserves the
        # output so a mid-session crash can be reported with its final logs.
        self._stdout_chunks: list[bytes] = []
        self._stderr_chunks: list[bytes] = []
        self._reader_threads: list[threading.Thread] = []
        #: Exit code of the most recently stopped server process. Negative
        #: values are -signal (e.g. -11 for SIGSEGV); None until the process
        #: has been reaped by stop().
        self.last_exit_code: int | None = None

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

    @property
    def provenance_instance_uuid(self) -> str:
        """The provenance instance UUID sent to the server at launch.

        Clients using this UUID can be authorized to request server shutdown,
        as they match the launch provenance.
        """
        return self._provenance_instance_uuid

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
        # BASIC ROM is auto-loaded by the server if present in the ROM directory.
        # If a custom BASIC filepath is provided, configure it via sideways ROM slot 15.
        if self._basic_filepath:
            cmd.extend(["--sideways", f"15:rom:{self._basic_filepath}"])

        # Add provenance flags
        cmd.extend(
            [
                "--provenance-type",
                "python-client",
                "--provenance-uuid",
                self._provenance_instance_uuid,
                "--provenance-version",
                beebium.__version__,
            ]
        )

        # Add any extra arguments (e.g., --tube 65C02-3MHz)
        cmd.extend(self._extra_args)

        # Start the server process
        self._process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._start_readers()

        # Wait for the server to be ready
        if not self._wait_for_ready(timeout):
            exit_code = self._process.poll() if self._process is not None else None
            self.stop(timeout=1.0)  # joins the reader threads and reaps output
            stdout_output, stderr_output = self._captured_output()

            # Build detailed error message
            error_msg = f"Server failed to start within {timeout} seconds"
            error_msg += f"\nCommand: {' '.join(cmd)}"
            if exit_code is not None:
                error_msg += f"\nServer exited with {self._describe_exit(exit_code)}"
            if stderr_output:
                error_msg += f"\nServer stderr:\n{stderr_output}"
            if stdout_output:
                error_msg += f"\nServer stdout:\n{stdout_output}"
            raise ServerStartupError(error_msg)

    def stop(self, timeout: float = 5.0) -> None:
        """Stop the server gracefully, then forcefully if needed.

        If the server has already exited on its own before this call -- i.e. it
        crashed mid-session rather than being shut down -- its exit signal and
        captured stderr are written to this process's stderr so the failure is
        visible (e.g. in a pytest run) instead of being silently swallowed.

        Args:
            timeout: Maximum time to wait for graceful shutdown (seconds).
        """
        proc = self._process
        if proc is None:
            return

        # A non-None poll() before we have asked it to stop means the server
        # died on its own -- an unexpected mid-session crash worth reporting.
        died_unexpectedly = proc.poll() is not None

        if proc.poll() is None:
            proc.terminate()  # graceful shutdown first (SIGTERM)
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                proc.kill()  # force kill if graceful shutdown didn't work
                proc.wait(timeout=1.0)

        # The process is gone, so its pipes are at EOF; the readers will finish.
        for thread in self._reader_threads:
            thread.join(timeout=2.0)
        self._reader_threads = []
        self.last_exit_code = proc.returncode
        self._process = None

        if died_unexpectedly:
            self._report_unexpected_exit(proc.returncode)

    def _start_readers(self) -> None:
        """Spawn daemon threads to drain the server's stdout/stderr pipes.

        With BEEBIUM_SERVER_STDERR=1 the drained output is also teed live to
        this process's own stdout/stderr, so server logging -- in particular the
        opt-in BEEBIUM_*_TRACE diagnostics, which the server writes to stderr --
        is visible in the terminal instead of only being surfaced on a crash.
        """
        tee = os.environ.get("BEEBIUM_SERVER_STDERR") == "1"

        def pump(pipe: object, sink: list[bytes], mirror: object) -> None:
            try:
                for chunk in iter(lambda: pipe.read(4096), b""):  # type: ignore[union-attr]
                    sink.append(chunk)
                    if mirror is not None:
                        # Write bytes straight to the underlying binary buffer so
                        # a chunk that splits a multi-byte character is not
                        # mangled; fall back to a decoded write if there is none.
                        buffer = getattr(mirror, "buffer", None)
                        if buffer is not None:
                            buffer.write(chunk)  # type: ignore[union-attr]
                        else:
                            mirror.write(chunk.decode("utf-8", errors="replace"))  # type: ignore[union-attr]
                        mirror.flush()  # type: ignore[union-attr]
            except (ValueError, OSError):
                pass  # pipe closed underneath us during shutdown

        assert self._process is not None
        for pipe, sink, mirror in (
            (self._process.stdout, self._stdout_chunks, sys.stdout if tee else None),
            (self._process.stderr, self._stderr_chunks, sys.stderr if tee else None),
        ):
            thread = threading.Thread(target=pump, args=(pipe, sink, mirror), daemon=True)
            thread.start()
            self._reader_threads.append(thread)

    def _captured_output(self) -> tuple[str, str]:
        """Decode everything the reader threads have collected so far."""
        return (
            b"".join(self._stdout_chunks).decode("utf-8", errors="replace"),
            b"".join(self._stderr_chunks).decode("utf-8", errors="replace"),
        )

    @staticmethod
    def _describe_exit(code: int) -> str:
        """Human-readable description of a subprocess exit code."""
        if code < 0:
            try:
                name = signal.Signals(-code).name
            except ValueError:
                name = f"signal {-code}"
            return f"signal {-code} ({name})"
        return f"code {code}"

    def _report_unexpected_exit(self, code: int | None) -> None:
        """Write a mid-session crash report to stderr (captured by pytest)."""
        _, stderr_output = self._captured_output()
        detail = self._describe_exit(code) if code is not None else "unknown status"
        report = f"\n[beebium] server {self._server_filepath} exited unexpectedly mid-session with {detail}."
        if stderr_output:
            report += f"\n[beebium] server stderr:\n{stderr_output}"
        print(report, file=sys.stderr, flush=True)

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

    @staticmethod
    def _is_executable(filepath: Path) -> bool:
        """Check if a file is executable (cross-platform)."""
        if not filepath.exists():
            return False
        if sys.platform == "win32":
            # On Windows, check for common executable extensions
            return filepath.suffix.lower() in (".exe", ".cmd", ".bat", ".com")
        else:
            return os.access(filepath, os.X_OK)

    @staticmethod
    def _exe_name(name: str) -> str:
        """Return executable name with platform-appropriate extension."""
        if sys.platform == "win32" and not name.lower().endswith(".exe"):
            return name + ".exe"
        return name

    def _find_in_repo_build(self, exe_name: str) -> Path | None:
        """Find the server in a build directory of the surrounding checkout.

        Returns None when this package is not sitting in a source checkout with
        a build directory, which is the case for an installed wheel.
        """
        # Build directory candidates (Unix and Windows)
        build_dirs = [
            "build",
            "cmake-build-debug",
            "cmake-build-release",
        ]
        if sys.platform == "win32":
            build_dirs.extend(
                [
                    "build-win-x64-release",
                    "build-win-x64-debug",
                    "out/build/x64-Release",
                    "out/build/x64-Debug",
                ]
            )

        # Walk up from this file rather than counting parents to a fixed depth.
        # A previous fixed count silently stopped one level short of the repo
        # root, so this search never matched and PATH always won -- which is
        # exactly the failure this function exists to prevent. Searching every
        # ancestor cannot go stale when a directory is renamed or moved.
        for ancestor in Path(__file__).resolve().parents:
            for build_dir in build_dirs:
                server_dirpath = ancestor / build_dir / "src" / "server"
                if sys.platform == "win32":
                    # With MSVC the executable lands in a per-config subdirectory.
                    for config in ["Release", "Debug", ""]:
                        candidate = server_dirpath / config / exe_name if config \
                            else server_dirpath / exe_name
                        if self._is_executable(candidate):
                            return candidate
                else:
                    candidate = server_dirpath / exe_name
                    if self._is_executable(candidate):
                        return candidate

        return None

    def _find_server(self, path: str | Path | None) -> Path:
        """Find the beebium-model-b executable.

        Search order:
        1. Explicit path argument
        2. BEEBIUM_SERVER environment variable
        3. A build directory in the surrounding source checkout
        4. PATH lookup for 'beebium-model-b'

        The in-repo build deliberately outranks PATH. A developer with a
        release of Beebium installed system-wide would otherwise test their
        working tree's client against the installed server, which silently
        pairs a client and server built from different protocols -- the
        fingerprint handshake then fails at connect with no hint that the wrong
        binary was chosen. A build directory only exists in a checkout, so
        preferring it cannot affect an installed wheel, where step 3 finds
        nothing and PATH is used as before.
        """
        # 1. Explicit path
        if path is not None:
            explicit = Path(path)
            if self._is_executable(explicit):
                return explicit
            raise ServerNotFoundError(f"Server not found at specified path: {path}")

        # 2. Environment variable
        env_path = os.environ.get("BEEBIUM_SERVER")
        if env_path:
            env_server = Path(env_path)
            if self._is_executable(env_server):
                return env_server
            raise ServerNotFoundError(f"BEEBIUM_SERVER points to invalid path: {env_path}")

        exe_name = self._exe_name("beebium-model-b")

        # 3. A build directory in the surrounding source checkout
        in_repo = self._find_in_repo_build(exe_name)
        if in_repo is not None:
            return in_repo

        # 4. PATH lookup
        which_result = shutil.which(exe_name)
        if which_result:
            return Path(which_result)

        raise ServerNotFoundError(
            "beebium-model-b not found. Set BEEBIUM_SERVER environment variable or add beebium-model-b to PATH."
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

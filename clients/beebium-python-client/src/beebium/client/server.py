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
import signal
import socket
import subprocess
import sys
import threading
import time
import uuid
import warnings
from pathlib import Path

import grpc

import beebium.client as beebium
from beebium.client.exceptions import ServerStartupError
from beebium.client.installation import DEFAULT_VARIANT, ServerInstallation, find_in_build_tree


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
        mos_filepath: str | Path | None = None,
        basic_filepath: str | Path | None = None,
        server: ServerInstallation | str | Path | None = None,
        variant: str = DEFAULT_VARIANT,
        server_filepath: str | Path | None = None,
        port: int = 0,
        extra_args: list[str] | None = None,
    ):
        """Create a server process manager.

        Args:
            mos_filepath: Path to the MOS ROM file. When None, ``--mos`` is not
                passed and the server resolves its default MOS from its own ROM
                directory (so a wheel/bundle install launches with no arguments).
            basic_filepath: Path to the BASIC ROM file (optional).
            server: which server to run -- a ServerInstallation, a path to a
                binary, or a path to an install root. When None, the default
                resolution applies (BEEBIUM_SERVER, checkout build, the
                beebium-server wheel, then PATH).
            variant: the machine variant within the installation ("model-b").
            server_filepath: deprecated alias for ``server=`` (a binary path).
            port: Port to listen on. If 0 (default), a free port is allocated.
            extra_args: Additional command-line arguments to pass to the server
                (e.g., ["--tube", "65C02-3MHz"]).
        """
        if server_filepath is not None:
            if server is not None:
                raise ValueError("pass server=, not both server= and the deprecated server_filepath=")
            warnings.warn(
                "server_filepath= is deprecated; use server= (a ServerInstallation, "
                "a binary path, or an install root)",
                DeprecationWarning,
                stacklevel=2,
            )
            server = server_filepath

        self._mos_filepath = Path(mos_filepath) if mos_filepath is not None else None
        self._basic_filepath = Path(basic_filepath) if basic_filepath else None
        self._variant = variant
        self._installation = (
            ServerInstallation.default() if server is None else ServerInstallation.coerce(server)
        )
        self._server_filepath = self._installation.executable_filepath(variant)
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
    def server_filepath(self) -> Path:
        """Path to the resolved server executable."""
        return self._server_filepath

    @property
    def installation(self) -> ServerInstallation:
        """The installation this server was resolved from."""
        return self._installation

    @property
    def variant(self) -> str:
        """The machine variant running (e.g. ``model-b``)."""
        return self._variant

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

    def _build_command(self) -> list[str]:
        """The server command line. A None mos_filepath omits --mos, so the
        server resolves its own default MOS from its ROM directory."""
        cmd = [str(self._server_filepath)]
        if self._mos_filepath is not None:
            cmd.extend(["--mos", str(self._mos_filepath)])
        cmd.extend(["--port", str(self._port)])
        # BASIC ROM is auto-loaded by the server if present in the ROM directory.
        # A custom BASIC filepath is configured via sideways ROM slot 15.
        if self._basic_filepath:
            cmd.extend(["--sideways", f"15:rom:{self._basic_filepath}"])
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
        cmd.extend(self._extra_args)
        return cmd

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
        if self._mos_filepath is not None and not self._mos_filepath.exists():
            raise ServerStartupError(f"MOS ROM not found: {self._mos_filepath}")
        if self._basic_filepath and not self._basic_filepath.exists():
            raise ServerStartupError(f"BASIC ROM not found: {self._basic_filepath}")

        cmd = self._build_command()

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
                # A server that ignores SIGTERM is a server-side defect, and
                # killing it quietly hides that: say so, then force the issue
                # so a stuck server cannot outlive the session.
                print(
                    f"beebium: server (pid {proc.pid}) ignored a shutdown request for "
                    f"{timeout}s; killing it",
                    file=sys.stderr,
                )
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

    @staticmethod
    def _find_in_repo_build(
        exe_name: str, search_dirpaths: list[Path] | None = None
    ) -> Path | None:
        """Find a server binary in a checkout build directory.

        Thin wrapper over :func:`beebium.client.installation.find_in_build_tree`,
        kept because the pytest plugin resolves the checkout build from pytest's
        rootdir through it.
        """
        return find_in_build_tree(exe_name, search_dirpaths)

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

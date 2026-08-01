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

"""A server can always be asked to stop, whatever the machine is doing.

These tests drive the real server executable, because the property under test
is a property of the process: an operating-system shutdown request must be
honoured even when the emulation loop is not running -- when the machine is
paused at its first instruction awaiting Run(), paused by the debugger, or has
not been started at all because the server is still waiting for RETURN.

The shutdown request is whatever the platform actually delivers: SIGTERM on
POSIX, a console CTRL_BREAK event on Windows (which needs the server in its own
process group, hence the creation flag).
"""

from __future__ import annotations

import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.server import ServerProcess

# Generous enough that a loaded CI runner does not produce a false failure,
# short enough that a genuine hang (which lasts forever) is still caught.
SHUTDOWN_TIMEOUT_SECONDS = 15.0

STARTUP_TIMEOUT_SECONDS = 20.0


@pytest.fixture
def server_filepath(mos_filepath: Path, beebium_server_filepath: Path | None) -> Path:
    """Path to the server executable these tests spawn directly."""
    try:
        return ServerProcess(
            mos_filepath=mos_filepath, server_filepath=beebium_server_filepath
        ).server_filepath
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        s.listen(1)
        return int(s.getsockname()[1])


def _spawn(server_filepath: Path, mos_filepath: Path, port: int, wait_mode: str):
    """Start a server in its own process group, with stdin not a terminal."""
    command = [
        str(server_filepath),
        "start",
        "--mos",
        str(mos_filepath),
        "--port",
        str(port),
        f"--wait={wait_mode}",
    ]
    creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
    return subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=creation_flags,
    )


def _wait_until_listening(process: subprocess.Popen, port: int) -> None:
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if process.poll() is not None:
            pytest.fail(f"Server exited during startup with code {process.returncode}")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.25)
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.1)
    pytest.fail(f"Server did not listen on port {port} within {STARTUP_TIMEOUT_SECONDS}s")


def _request_shutdown(process: subprocess.Popen) -> None:
    """Ask the process to stop the way the operating system does."""
    if sys.platform == "win32":
        os.kill(process.pid, signal.CTRL_BREAK_EVENT)
    else:
        process.send_signal(signal.SIGTERM)


def _assert_stops(process: subprocess.Popen, description: str) -> None:
    """Require the process to exit of its own accord; kill it if it does not."""
    try:
        process.wait(timeout=SHUTDOWN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5.0)
        pytest.fail(
            f"{description}: server ignored the shutdown request for "
            f"{SHUTDOWN_TIMEOUT_SECONDS}s and had to be killed"
        )


def test_stops_when_paused_awaiting_run(server_filepath: Path, mos_filepath: Path) -> None:
    """--wait=api pauses at the first instruction; it must still be stoppable.

    This is the default wait mode whenever stdin is not a terminal, which is
    how graphical front ends and test harnesses launch servers. A server that
    has not yet been told to run must still be able to be told to stop.
    """
    port = _free_port()
    process = _spawn(server_filepath, mos_filepath, port, "api")
    try:
        _wait_until_listening(process, port)
        _request_shutdown(process)
        _assert_stops(process, "paused awaiting Run()")
    finally:
        if process.poll() is None:
            process.kill()


def test_stops_when_waiting_for_return(server_filepath: Path, mos_filepath: Path) -> None:
    """--wait=cli blocks before the emulation loop exists; still stoppable.

    Nothing is ever written to the server's stdin here, so the wait is only
    ended by the shutdown request.
    """
    port = _free_port()
    process = _spawn(server_filepath, mos_filepath, port, "cli")
    try:
        _wait_until_listening(process, port)
        _request_shutdown(process)
        _assert_stops(process, "waiting for RETURN")
    finally:
        if process.poll() is None:
            process.kill()


def test_stops_when_paused_by_debugger(server_filepath: Path, mos_filepath: Path) -> None:
    """A machine stopped by the debugger must still be stoppable.

    This is the case that orphaned servers in practice: a test fixture leaves
    the machine stopped, and teardown's shutdown request then goes unheard.
    """
    port = _free_port()
    process = _spawn(server_filepath, mos_filepath, port, "api")
    try:
        _wait_until_listening(process, port)
        with Beebium.connect(target=f"localhost:{port}") as bbc:
            bbc.debugger.ensure_running()
            time.sleep(0.5)  # Let the emulation loop actually get going.
            bbc.debugger.stop()
            assert not bbc.debugger.is_running
        _request_shutdown(process)
        _assert_stops(process, "paused by the debugger")
    finally:
        if process.poll() is None:
            process.kill()


def test_stops_while_running(server_filepath: Path, mos_filepath: Path) -> None:
    """The unpaused case, as a control: this worked before and must keep working."""
    port = _free_port()
    process = _spawn(server_filepath, mos_filepath, port, "api")
    try:
        _wait_until_listening(process, port)
        with Beebium.connect(target=f"localhost:{port}") as bbc:
            bbc.debugger.ensure_running()
            time.sleep(0.5)
            assert bbc.debugger.is_running
        _request_shutdown(process)
        _assert_stops(process, "running")
    finally:
        if process.poll() is None:
            process.kill()

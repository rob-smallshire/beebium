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

"""Pin for the README's shell example: the console script starts a server.

This is NOT a rendered snippet -- the README shows the shell invocation as a
fenced sh block. This test runs the installed console script for real and
asserts it prints the "Listening on port" line the README documents, so that
block cannot drift into a lie.

POSIX only: the console script exec-replaces itself with the server, so
terminate() delivers the signal to the server directly and the port is released.
On Windows the shim runs the server as a child (os.execv cannot preserve console
signal semantics there), so terminating the shim would orphan it; the Windows
verify leg proves the console script another way (list-extensions).
"""

from __future__ import annotations

import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def _console_script_filepath() -> Path:
    # Console scripts install beside the interpreter running the tests.
    return Path(sys.executable).parent / "beebium-model-b"


@pytest.mark.skipif(sys.platform == "win32", reason="Windows shim runs the server as a child; see module docstring")
def test_console_script_starts_a_server():
    console_script_filepath = _console_script_filepath()
    assert console_script_filepath.exists(), f"console script not installed: {console_script_filepath}"

    port = _free_port()
    process = subprocess.Popen(
        [str(console_script_filepath), "--port", str(port)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        listening_line = None
        deadline = time.monotonic() + 30.0
        assert process.stdout is not None
        while time.monotonic() < deadline:
            line = process.stdout.readline()
            if line == "" and process.poll() is not None:
                break
            if "Listening on port" in line:
                listening_line = line.strip()
                break
        assert listening_line is not None, "server never reported 'Listening on port'"
        assert str(port) in listening_line
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)

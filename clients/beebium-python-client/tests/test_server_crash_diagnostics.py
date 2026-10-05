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

"""The server-process harness must not silently swallow a mid-session crash.

When a launched beebium-server dies in the middle of a session (a crash, not a
clean shutdown) the only client-visible symptom is an opaque gRPC "Socket
closed". These tests pin the harness behaviour that turns that into an
actionable report: the server's output is drained continuously (so a chatty or
crashing server is captured and cannot deadlock on a full pipe), and an
unexpected exit is reported with its signal.
"""

from __future__ import annotations

import os
import signal
import sys
import time

import pytest

from beebium.client.exceptions import ServerNotFoundError, ServerStartupError
from beebium.client.server import ServerProcess

# The crash path is exercised with POSIX signal semantics.
pytestmark = pytest.mark.skipif(sys.platform.startswith("win"), reason="POSIX signal semantics")


def _server(mos_filepath, basic_filepath, beebium_server_filepath) -> ServerProcess:
    # beebium_server_filepath is None when neither --beebium-server nor an
    # explicit path is given; ServerProcess then auto-detects via BEEBIUM_SERVER
    # and the usual build locations.
    try:
        return ServerProcess(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
        )
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_server_output_is_drained_during_the_session(mos_filepath, basic_filepath, beebium_server_filepath):
    """The reader threads capture the server's output rather than discarding it."""
    server = _server(mos_filepath, basic_filepath, beebium_server_filepath)
    try:
        server.start()
        assert server.is_running
    finally:
        server.stop()
    stdout, stderr = server._captured_output()
    assert stdout or stderr, "expected the server to have emitted a startup banner"


def test_midsession_crash_is_reported_with_its_signal(capsys, mos_filepath, basic_filepath, beebium_server_filepath):
    """A server that dies mid-session is reported, not silently swallowed."""
    server = _server(mos_filepath, basic_filepath, beebium_server_filepath)
    server.start()
    assert server.is_running

    # SIGKILL stands in for an uncatchable mid-session crash.
    os.kill(server._process.pid, signal.SIGKILL)
    deadline = time.monotonic() + 5.0
    while server.is_running and time.monotonic() < deadline:
        time.sleep(0.05)
    assert not server.is_running, "server did not die after SIGKILL"

    server.stop()

    assert server.last_exit_code == -signal.SIGKILL
    report = capsys.readouterr().err
    assert "exited unexpectedly" in report
    assert "SIGKILL" in report


def test_clean_shutdown_is_not_reported_as_a_crash(capsys, mos_filepath, basic_filepath, beebium_server_filepath):
    """A normal stop() must not emit a crash report."""
    server = _server(mos_filepath, basic_filepath, beebium_server_filepath)
    server.start()
    server.stop()
    assert "exited unexpectedly" not in capsys.readouterr().err


def _fake_server(tmp_path, body: str) -> ServerProcess:
    """A stand-in server executable that behaves as the shell `body` says."""
    script = tmp_path / "beebium-model-b"
    script.write_text(f"#!/bin/sh\n{body}\n")
    script.chmod(0o755)
    return ServerProcess(server=script)


def test_a_server_that_dies_during_startup_is_reported_as_dying(tmp_path, capsys):
    """An instant death is an exit, not a timeout: the message says so first."""
    server = _fake_server(tmp_path, "echo dying >&2; kill -PIPE $$")
    started = time.monotonic()
    with pytest.raises(ServerStartupError) as excinfo:
        server.start(timeout=10.0)
    assert time.monotonic() - started < 5.0

    message = str(excinfo.value)
    assert message.startswith("Server exited during startup after ")
    assert "with signal 13 (SIGPIPE), before reporting ready" in message.splitlines()[0]
    assert "within" not in message
    assert "dying" in message  # the server's own stderr is still attached
    # The startup error is the report; no second, mid-session one.
    assert "mid-session" not in capsys.readouterr().err


def test_a_server_that_exits_during_startup_gives_its_exit_code(tmp_path):
    server = _fake_server(tmp_path, "exit 3")
    with pytest.raises(ServerStartupError) as excinfo:
        server.start(timeout=10.0)
    assert "with code 3, before reporting ready" in str(excinfo.value).splitlines()[0]


def test_a_server_that_never_becomes_ready_is_reported_as_a_timeout(tmp_path):
    server = _fake_server(tmp_path, "exec sleep 30")
    with pytest.raises(ServerStartupError) as excinfo:
        server.start(timeout=0.5)
    assert str(excinfo.value).startswith("Server failed to start within 0.5 seconds")

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

"""A launched server does not outlive the Python process that launched it (#254).

Each test runs the launching client as a child Python process, so the child can
be killed outright, as a tool's timeout kills a process tree, or exit without
closing. The child prints the server's process id and address, then does as
the test says.
"""

from __future__ import annotations

import os
import subprocess
import sys
import textwrap
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium

EXIT_TIMEOUT = 15.0

LAUNCHER = textwrap.dedent(
    """
    import sys, time
    from beebium.client import Beebium

    server = sys.argv[1] or None
    action = sys.argv[2]
    bbc = Beebium.launch(server=server, exit_with_client=(action != "no-watch"))
    pid = bbc._server._process.pid  # private, as nothing public needs it
    if action == "detach":
        bbc.detach()
    print(pid, bbc.target, flush=True)
    if action == "exit":
        sys.exit(0)  # without close(): only the atexit hook stops the server
    if action == "detach":
        bbc.close()  # disconnects only
        sys.exit(0)
    time.sleep(600)  # until killed
    """
)


def _is_running(pid: int) -> bool:
    if sys.platform == "win32":
        import ctypes

        synchronize = 0x00100000
        handle = ctypes.windll.kernel32.OpenProcess(synchronize, False, pid)
        if not handle:
            return False
        try:
            return ctypes.windll.kernel32.WaitForSingleObject(handle, 0) != 0  # WAIT_OBJECT_0
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


def _wait_until_gone(pid: int, timeout: float = EXIT_TIMEOUT) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not _is_running(pid):
            return True
        time.sleep(0.1)
    return False


@pytest.fixture
def run_launcher(beebium_server_filepath: Path | None) -> Iterator:
    """Start the launcher script with an action; read the server's pid and target.

    Any server a test leaves running is stopped afterwards, by its pid.
    """
    started: list[int] = []
    children: list[subprocess.Popen] = []

    def run(action: str) -> tuple[subprocess.Popen, int, str]:
        child = subprocess.Popen(
            [sys.executable, "-c", LAUNCHER,
             str(beebium_server_filepath or ""), action],
            stdout=subprocess.PIPE,
            text=True,
        )
        children.append(child)
        line = child.stdout.readline()
        if not line:
            child.wait(timeout=30)
            pytest.skip("the launcher could not start a server")
        pid_text, target = line.split()
        started.append(int(pid_text))
        return child, int(pid_text), target

    yield run

    for child in children:
        if child.poll() is None:
            child.kill()
        child.wait(timeout=10)
    for pid in started:
        if _is_running(pid):
            if sys.platform == "win32":
                subprocess.run(["taskkill", "/PID", str(pid), "/F"], check=False)
            else:
                os.kill(pid, 15)
            _wait_until_gone(pid)


def test_a_server_exits_when_its_launcher_is_killed(run_launcher) -> None:
    child, server_pid, _ = run_launcher("wait")
    assert _is_running(server_pid)

    child.kill()  # SIGKILL on POSIX, TerminateProcess on Windows: no cleanup runs
    child.wait(timeout=10)

    assert _wait_until_gone(server_pid), "the server outlived its killed launcher"


def test_without_the_watch_a_killed_launcher_leaves_its_server(run_launcher) -> None:
    # The control: launched with exit_with_client=False, the same kill leaves
    # the server running, so the watch is what ends it above.
    child, server_pid, _ = run_launcher("no-watch")
    child.kill()
    child.wait(timeout=10)

    time.sleep(2.0)
    assert _is_running(server_pid)


def test_the_interpreter_exit_stops_an_unclosed_launch(run_launcher) -> None:
    child, server_pid, _ = run_launcher("exit")
    assert child.wait(timeout=30) == 0

    assert _wait_until_gone(server_pid), "an unclosed launch outlived the interpreter"


def test_a_detached_server_runs_on_after_its_launcher_exits(run_launcher) -> None:
    child, server_pid, target = run_launcher("detach")
    assert child.wait(timeout=30) == 0

    # The launcher, and its pipes, are gone. The server, released, runs on:
    # neither the end of its stdin nor a write to its departed stdout ends it.
    time.sleep(2.0)
    assert _is_running(server_pid)
    bbc = Beebium.connect(target)
    try:
        assert bbc.system.request_shutdown().accepted  # the sole client may stop it
    finally:
        bbc.close()
    assert _wait_until_gone(server_pid)


def test_only_the_launcher_may_detach(beebium_server_filepath: Path | None) -> None:
    from beebium.client.exceptions import BeebiumError, ServerNotFoundError

    try:
        launched = Beebium.launch(server=beebium_server_filepath)
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    try:
        other = Beebium.connect(launched.target)
        try:
            with pytest.raises(BeebiumError):
                other.detach()  # it launched nothing
            with pytest.raises(BeebiumError):
                other.system.release_from_launcher()  # nor may it release
        finally:
            other.close()
    finally:
        launched.close()

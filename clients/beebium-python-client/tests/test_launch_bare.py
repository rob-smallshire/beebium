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

"""Beebium.launch() returns the client, usable bare or under with (#245).

Like open() and connect(), a launch hands back the Beebium itself, so a REPL
needs no ``__enter__()``; close() stops the server it started. When the server
starts but the client cannot connect to it or verify it, the server is
stopped before the error is raised.
"""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

import pytest

import beebium.client.client
from beebium.client import Beebium, ProtocolMismatchError, ServerNotFoundError
from beebium.client.server import ServerProcess


@pytest.fixture
def started_servers(monkeypatch: pytest.MonkeyPatch) -> Iterator[list[ServerProcess]]:
    """Every ServerProcess started during the test, to check it was stopped."""
    started: list[ServerProcess] = []
    original_start = ServerProcess.start

    def recording_start(self: ServerProcess, timeout: float = 10.0) -> None:
        started.append(self)
        original_start(self, timeout=timeout)

    monkeypatch.setattr(ServerProcess, "start", recording_start)
    yield started
    for process in started:  # leave nothing running whatever the outcome
        process.stop()


def test_launch_returns_a_running_client_that_close_shuts_down(
    beebium_server_filepath: Path | None, started_servers: list[ServerProcess]
) -> None:
    try:
        bbc = Beebium.launch(server=beebium_server_filepath)
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    try:
        assert isinstance(bbc, Beebium)
        assert bbc.debugger.is_running
        assert [p.is_running for p in started_servers] == [True]
    finally:
        bbc.close()
    assert [p.is_running for p in started_servers] == [False]


def test_launch_under_with_shuts_the_server_down_on_exit(
    beebium_server_filepath: Path | None, started_servers: list[ServerProcess]
) -> None:
    try:
        with Beebium.launch(server=beebium_server_filepath) as bbc:
            assert bbc.debugger.is_running
            assert [p.is_running for p in started_servers] == [True]
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    assert [p.is_running for p in started_servers] == [False]


def test_a_failed_verification_stops_the_started_server(
    monkeypatch: pytest.MonkeyPatch,
    beebium_server_filepath: Path | None,
    started_servers: list[ServerProcess],
) -> None:
    monkeypatch.setattr(beebium.client.client, "PROTOCOL_FINGERPRINT", "mismatched-fingerprint")
    try:
        with pytest.raises(ProtocolMismatchError):
            Beebium.launch(server=beebium_server_filepath)
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    assert len(started_servers) == 1
    assert not started_servers[0].is_running


def test_a_failed_connection_stops_the_started_server(
    monkeypatch: pytest.MonkeyPatch,
    beebium_server_filepath: Path | None,
    started_servers: list[ServerProcess],
) -> None:
    class Unreachable(Exception):
        pass

    def refuse(*args: object, **kwargs: object) -> None:
        raise Unreachable("no connection")

    monkeypatch.setattr(beebium.client.client, "Connection", refuse)
    try:
        with pytest.raises(Unreachable):
            Beebium.launch(server=beebium_server_filepath)
    except ServerNotFoundError as e:
        pytest.skip(str(e))
    assert len(started_servers) == 1
    assert not started_servers[0].is_running

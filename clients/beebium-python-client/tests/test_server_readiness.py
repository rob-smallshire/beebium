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

"""A launched server is seen as soon as it listens, whatever came before it.

A test session launches hundreds of servers, each on a free port the kernel
picks, so a port can come round again. A channel to the stopped server that
used it before backs off between reconnect attempts, longer the longer it has
failed. Had the readiness probe shared that channel's connection, it would not
see the new server until the backoff ran out: in CI a server that had printed
its ready line was declared not started after 20 seconds.
"""

from __future__ import annotations

import socket
import sys
import time
from concurrent import futures

import grpc

from beebium.client.server import ServerProcess


def _free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("localhost", 0))
        return sock.getsockname()[1]


def _serve(target: str) -> grpc.Server:
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=1))
    server.add_insecure_port(target)
    server.start()
    return server


def test_a_server_on_a_port_reused_from_a_stopped_one_is_seen_at_once() -> None:
    port = _free_port()
    target = f"localhost:{port}"

    earlier = _serve(target)
    stale = grpc.insecure_channel(target)  # a client of the earlier server, still open
    try:
        grpc.channel_ready_future(stale).result(timeout=5.0)
        stale.subscribe(lambda _state: None, try_to_connect=True)
        earlier.stop(None).wait()
        time.sleep(10.0)  # the stale channel keeps failing and its backoff grows

        later = _serve(target)
        try:
            process = ServerProcess(server=sys.executable, port=port)
            started = time.monotonic()
            assert process._wait_for_ready(timeout=10.0)
            assert time.monotonic() - started < 1.0
        finally:
            later.stop(None)
    finally:
        stale.close()

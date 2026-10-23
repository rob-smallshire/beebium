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

"""Integration test for the printer port with the tcp-printer extension (#99).

A listening socket in the test stands in for a raw TCP printer (a JetDirect
port, ``nc -l 9100``, or Papyrium). A real Model B launched with
``--tcp-printer`` prints through the MOS driver and the bytes arrive at the
socket. Every wait polls the guest, the socket or the server's own
accounting, never the wall clock alone.
"""

from __future__ import annotations

import socket
import threading
import time
from collections.abc import Iterator

import pytest

from beebium.client import Beebium
from beebium.ext.peripheral.tcp_printer import TcpPrinter

TIMEOUT = 20.0


class _RecordingPrinter:
    """A TCP listener on 127.0.0.1 that records what its client sends."""

    def __init__(self) -> None:
        self._server = socket.create_server(("127.0.0.1", 0))
        self._server.settimeout(0.1)
        self.port: int = self._server.getsockname()[1]
        self._lock = threading.Lock()
        self._received = bytearray()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    @property
    def received(self) -> bytes:
        with self._lock:
            return bytes(self._received)

    def close(self) -> None:
        self._stop.set()
        self._thread.join()
        self._server.close()

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                client, _ = self._server.accept()
            except TimeoutError:
                continue
            with client:
                client.settimeout(0.1)
                while not self._stop.is_set():
                    try:
                        chunk = client.recv(256)
                    except TimeoutError:
                        continue
                    if not chunk:
                        break
                    with self._lock:
                        self._received.extend(chunk)


@pytest.fixture
def recording_printer() -> Iterator[_RecordingPrinter]:
    printer = _RecordingPrinter()
    try:
        yield printer
    finally:
        printer.close()


def _screen(bbc: Beebium) -> str:
    text = bbc.video.screen_text().text
    return "\n".join(text) if isinstance(text, list) else text


def _wait_until(condition, description: str, timeout: float = TIMEOUT) -> None:
    """Poll `condition` until it holds, rather than sleeping blind."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.05)
    pytest.fail(f"timed out waiting for {description}")


def _type_line(bbc: Beebium, line: str) -> None:
    bbc.keyboard.type(line + "\r")
    bbc.keyboard.wait_until_typing_complete()


# The marker is assembled at run time so it is on the screen only once the
# statement after the print has executed, not as part of the echoed line.
MARKER_EXPR = '"DO"+"NE"'
MARKER = "DONE"


def test_mos_printer_driver_sends_hello_with_cr_to_the_socket(launch_bbc, recording_printer: _RecordingPrinter) -> None:
    """*FX5,1 then VDU 2 / PRINT / VDU 3 sends HELLO CR: *FX6,10 suppresses the LF."""
    bbc = launch_bbc(extra_args=["--tcp-printer", f"host=127.0.0.1:port={recording_printer.port}"])
    bbc.wait_until_ready()
    printer = bbc.extensions[TcpPrinter]
    _wait_until(lambda: printer.get_status().connected, "the printer to connect")

    _type_line(bbc, "*FX5,1")
    _type_line(bbc, f'VDU 2:PRINT "HELLO":VDU 3:PRINT {MARKER_EXPR}')
    _wait_until(lambda: MARKER in _screen(bbc), "the print to finish")
    _wait_until(lambda: printer.get_status().bytes_sent >= 6, "six bytes sent")
    _wait_until(lambda: len(recording_printer.received) >= 6, "six bytes at the socket")

    assert recording_printer.received == b"HELLO\r"
    status = printer.get_status()
    assert status.endpoint == f"127.0.0.1:{recording_printer.port}"
    assert status.connected
    assert status.bytes_received == 6
    assert status.bytes_sent == 6

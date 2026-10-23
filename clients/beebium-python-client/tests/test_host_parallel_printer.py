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

"""Integration test for the printer port with the host-parallel-printer extension (#99).

No machine on the project has a printer device, so a FIFO stands in for one:
it keeps the device-file contract the extension relies on (open fails with no
reader, as with an absent printer, and bytes written arrive in order). The
test holds the read end, launches a real Model B with
``--host-parallel-printer device=<fifo>`` and prints through the MOS driver.
POSIX only: Windows has no FIFO with these semantics. Every wait polls the
guest, the FIFO or the server's own accounting, never the wall clock alone.
"""

from __future__ import annotations

import os
import sys
import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.ext.peripheral.host_parallel_printer import HostParallelPrinter

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="needs a POSIX FIFO")

TIMEOUT = 20.0


class _FifoPrinter:
    """The read end of a FIFO standing in for a printer device."""

    def __init__(self, fifo_path: Path) -> None:
        os.mkfifo(fifo_path)
        self.path = fifo_path
        # Non-blocking, so the read end exists before any writer does.
        self._fd = os.open(fifo_path, os.O_RDONLY | os.O_NONBLOCK)
        self._received = bytearray()

    @property
    def received(self) -> bytes:
        while True:
            try:
                chunk = os.read(self._fd, 256)
            except BlockingIOError:
                break
            if not chunk:
                break
            self._received.extend(chunk)
        return bytes(self._received)

    def close(self) -> None:
        os.close(self._fd)


@pytest.fixture
def fifo_printer(tmp_path: Path) -> Iterator[_FifoPrinter]:
    printer = _FifoPrinter(tmp_path / "lp0")
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


def test_mos_printer_driver_writes_hello_with_cr_to_the_device(launch_bbc, fifo_printer: _FifoPrinter) -> None:
    """*FX5,1 then VDU 2 / PRINT / VDU 3 writes HELLO CR: *FX6,10 suppresses the LF."""
    bbc = launch_bbc(extra_args=["--host-parallel-printer", f"device={fifo_printer.path}"])
    bbc.wait_until_ready()
    printer = bbc.extensions[HostParallelPrinter]
    _wait_until(lambda: printer.get_status().open, "the device to open")

    _type_line(bbc, "*FX5,1")
    _type_line(bbc, f'VDU 2:PRINT "HELLO":VDU 3:PRINT {MARKER_EXPR}')
    _wait_until(lambda: MARKER in _screen(bbc), "the print to finish")
    _wait_until(lambda: printer.get_status().bytes_sent >= 6, "six bytes written")
    _wait_until(lambda: len(fifo_printer.received) >= 6, "six bytes at the FIFO")

    assert fifo_printer.received == b"HELLO\r"
    status = printer.get_status()
    assert status.device == str(fifo_printer.path)
    assert status.open
    assert status.bytes_received == 6
    assert status.bytes_sent == 6
    assert all(device.path and device.name for device in printer.list_devices())

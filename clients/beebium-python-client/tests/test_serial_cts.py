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

"""End-to-end /CTS back-pressure test driven by real BBC BASIC.

Unlike test_serial_extensions (which pokes the ACIA registers directly), this
exercises the full MOS serial driver: BASIC directs output to the RS423 port
(*FX3,1) and transmits a payload larger than the device's TX buffer. With the
rpc-serial client refusing to drain, the device's buffer fills, the Serial ULA
asserts the ACIA's /CTS, and the MOS serial OSWRCH path blocks -- the *guest*
stalls while the *host* stays responsive. Draining on the client releases /CTS
and the guest runs to completion.

Synchronisation is by feedback, not by fixed delays: each step runs the machine
until a polled condition holds (the BASIC prompt, the device buffer filling, a
completion sentinel), advancing emulated time so the result is independent of
host/CI wall-clock speed. The TX buffer is shrunk via tx_buffer so /CTS engages
after a handful of bytes.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains
from beebium.ext.peripheral.rpc_serial import RpcSerial

# Zero-page (user space) sentinel the program sets when it completes. A memory
# sentinel is unambiguous; a screen one would collide with the program listing
# echoed above the RUN output.
_DONE = 0x70
_PAYLOAD = 128  # bytes transmitted; must exceed the TX buffer + MOS buffer
_TX_BUFFER = 16  # small, so /CTS engages quickly

# 10 route output to RS423; 20 transmit the payload (VDU 65 -> OSWRCH 'A');
# 30 restore screen output; 40 set the completion sentinel.
_PROGRAM = [
    ("10 *FX3,1", "*FX3,1"),
    (f"20 FORI%=1TO{_PAYLOAD}:VDU65:NEXT", "VDU65"),
    ("30 *FX3,0", "*FX3,0"),
    (f"40 ?&{_DONE:X}=42", "=42"),
]


@pytest.fixture
def cts_bbc(mos_filepath, basic_filepath, beebium_server_filepath):
    """A BBC whose serial port is a small-buffered rpc-serial peer."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--rpc-serial", f"tx_buffer={_TX_BUFFER}"],
        ) as instance:
            instance.debugger.stop()
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _until(bbc, predicate, *, budget=60.0):
    """Run until predicate() holds, advancing emulated time. Fails on timeout."""
    assert bbc.run_until_or_timeout(predicate, emulated_seconds=budget), "timed out waiting for the polled condition"


def test_full_buffer_asserts_cts_and_stalls_only_the_guest(cts_bbc):
    bbc = cts_bbc
    rpc_serial = bbc.extensions[RpcSerial]

    _until(bbc, lambda: screen_contains(bbc, ">"))  # BASIC is ready
    bbc.memory.address.bus[_DONE] = 0

    for line, echo in _PROGRAM:
        bbc.keyboard.type(line + "\r")
        _until(bbc, lambda e=echo: screen_contains(bbc, e))  # line was entered
    bbc.keyboard.type("RUN\r")

    # The guest transmits until the device buffer fills and the ULA asserts /CTS;
    # with the client refusing to drain it stalls there -- yet the host stays
    # responsive (these gRPC calls keep returning).
    _until(bbc, lambda: rpc_serial.status.tx_pending >= _TX_BUFFER)
    assert bbc.memory.address.peek[_DONE] == 0, "guest must be blocked on the full buffer"

    # Drain on the client: each polled chunk frees buffer space, /CTS releases,
    # and the guest makes progress until the program completes.
    received = bytearray()

    def drained_to_completion():
        received.extend(rpc_serial.receive())
        return bbc.memory.address.peek[_DONE] == 42

    _until(bbc, drained_to_completion)

    assert bbc.memory.address.peek[_DONE] == 42, "guest should finish once the client drains"
    assert received.count(0x41) == _PAYLOAD, (
        f"all {_PAYLOAD} transmitted bytes should arrive, got {received.count(0x41)}"
    )

# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
# Copyright 2026 Mark J. Fisher
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

"""Serial port (MC6850 ACIA + Serial ULA) status client for beebium.

The serial port is the BBC Micro's on-board RS423 / cassette interface. This
module wraps ``SerialService``, which reports the live state of the serial
hardware registers.

What is attached to the far end of the wire is owned by a serial extension, not
by this service: launch the server with ``--rpc-serial`` (and drive it via
``bbc.extensions[RpcSerial]``), ``--host-serial`` (a real pty/device), or
``--loopback-serial`` (echo).
"""

from __future__ import annotations

from collections.abc import Iterator
from dataclasses import dataclass

from beebium.client._proto import serial_pb2, serial_pb2_grpc
from beebium.client._repr import facade_repr


@dataclass(frozen=True)
class SerialStatus:
    """Serial hardware status (MC6850 ACIA + Serial ULA)."""

    has_serial_socket: bool
    # Physical connector standard ("RS423" / "RS232"); empty when no socket.
    connector: str
    # MC6850 ACIA
    acia_control: int
    acia_status: int
    tdre: bool
    rdrf: bool
    not_dcd: bool
    not_cts: bool
    irq_pending: bool
    # Serial ULA (SERPROC)
    ula_control: int
    tx_baud: int
    rx_baud: int
    rs423_selected: bool
    motor_on: bool
    # ULA clocks (16/13 MHz) per transmit or receive clock for the selected
    # rates: 19200 is 1, 300 is 64, 75 is 256. The ACIA divides further.
    tx_ula_divider: int
    rx_ula_divider: int


def _response_to_status(response: serial_pb2.SerialStatus) -> SerialStatus:
    """Convert a SerialStatus proto into a SerialStatus dataclass."""
    return SerialStatus(
        has_serial_socket=response.has_serial_socket,
        connector=response.connector,
        acia_control=response.acia_control,
        acia_status=response.acia_status,
        tdre=response.tdre,
        rdrf=response.rdrf,
        not_dcd=response.not_dcd,
        not_cts=response.not_cts,
        irq_pending=response.irq_pending,
        ula_control=response.ula_control,
        tx_baud=response.tx_baud,
        rx_baud=response.rx_baud,
        rs423_selected=response.rs423_selected,
        motor_on=response.motor_on,
        tx_ula_divider=response.tx_ula_divider,
        rx_ula_divider=response.rx_ula_divider,
    )


class Serial:
    """Serial port (MC6850 ACIA + Serial ULA) status.

    Usage::

        status = bbc.serial.status
        print(f"tx_baud={status.tx_baud}, rdrf={status.rdrf}")
    """

    def __init__(self, stub: serial_pb2_grpc.SerialServiceStub):
        """Create a Serial interface.

        Args:
            stub: The gRPC stub for the SerialService.
        """
        self._stub = stub

    def __repr__(self) -> str:
        return facade_repr(self, "the serial port (MC6850 ACIA and Serial ULA)")

    @property
    def status(self) -> SerialStatus:
        """Get serial hardware status (ACIA + Serial ULA registers)."""
        request = serial_pb2.GetSerialStatusRequest()
        response = self._stub.GetSerialStatus(request)
        return _response_to_status(response)

    def watch_status(self, *, min_interval_ms: int = 0) -> Iterator[SerialStatus]:
        """Stream serial hardware status, pushed by the server on change.

        Yields an initial snapshot immediately, then a fresh one whenever the
        chip state changes (sampled at ``min_interval_ms``, so per-byte TDRE/RDRF
        toggling is coalesced). The stream stays open until the iterator is
        closed or the connection drops; prefer this over polling :attr:`status`.

        Args:
            min_interval_ms: Minimum interval between change checks (0 = server
                default of 50ms).
        """
        request = serial_pb2.WatchSerialStatusRequest(min_interval_ms=min_interval_ms)
        for response in self._stub.WatchSerialStatus(request):
            yield _response_to_status(response)

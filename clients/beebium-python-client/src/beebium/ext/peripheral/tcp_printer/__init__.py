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

"""Client for the tcp-printer extension's typed API.

The tcp-printer extension sends everything the BBC prints to a raw TCP
printer port on the network, such as a JetDirect printer on port 9100 or a
printer emulator. This client reads the endpoint, whether it is connected and
how much has been sent -- the scripting-friendly equivalent of the sidebar
panel. Requires the server to be launched with ``--tcp-printer``.

There is no way to switch the printer online or offline: it is online
exactly while it is connected.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client.extension import PeripheralExtensionAdapter
from beebium.ext.peripheral.tcp_printer._proto import tcp_printer_pb2

# The logical service name the tcp-printer extension's dispatcher registers
# (matches TcpPrinterDispatcher::service_name() in the extension).
_SERVICE = "TcpPrinter"


@dataclass(frozen=True)
class TcpPrinterStatus:
    """The endpoint, whether it is connected, and the byte counts."""

    endpoint: str  # host:port, an IPv6 host in brackets
    connected: bool  # connected, and so online
    bytes_received: int  # bytes accepted from the port since launch
    bytes_sent: int  # bytes written to the socket


class TcpPrinter(PeripheralExtensionAdapter):
    """Read the network printer's status.

    The TcpPrinter messages are tunnelled over the core's ExtensionRpc channel.

    Usage:
        printer = bbc.extensions[TcpPrinter]     # or TcpPrinter.attach(bbc)
        printer.get_status().connected
    """

    EXTENSION_NAME = "tcp-printer"

    def get_status(self) -> TcpPrinterStatus:
        """The endpoint, whether it is connected, and the byte counts."""
        request = tcp_printer_pb2.TcpPrinterGetStatusRequest()
        reply = self._invoke_bytes(_SERVICE, "GetStatus", request.SerializeToString())
        response = tcp_printer_pb2.TcpPrinterStatus()
        response.ParseFromString(reply)
        return TcpPrinterStatus(
            endpoint=response.endpoint,
            connected=response.connected,
            bytes_received=response.bytes_received,
            bytes_sent=response.bytes_sent,
        )

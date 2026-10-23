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

"""Client for the file-printer extension's typed API.

The file-printer extension spools everything the BBC sends to its printer
port into a file on the host. This client reads what it has written so far
and switches the printer online or offline -- the scripting-friendly
equivalent of the sidebar panel. Requires the server to be launched with
``--file-printer path=<file>``.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client.extension import PeripheralExtensionAdapter
from beebium.ext.peripheral.file_printer._proto import file_printer_pb2

# The logical service name the file-printer extension's dispatcher registers
# (matches FilePrinterDispatcher::service_name() in the extension).
_SERVICE = "FilePrinter"


@dataclass(frozen=True)
class FilePrinterStatus:
    """The file being written, the byte counts and the online state."""

    path: str  # the file being written
    bytes_received: int  # bytes accepted from the port since launch
    bytes_written: int  # bytes written to the file, after line-ending translation
    online: bool  # is the printer accepting bytes?


class FilePrinter(PeripheralExtensionAdapter):
    """Read the file printer's status and switch it online or offline.

    The FilePrinter messages are tunnelled over the core's ExtensionRpc channel.

    Usage:
        printer = bbc.extensions[FilePrinter]     # or FilePrinter.attach(bbc)
        printer.set_online(False)
    """

    EXTENSION_NAME = "file-printer"

    def get_status(self) -> FilePrinterStatus:
        """The file being written, the byte counts and the online state."""
        request = file_printer_pb2.FilePrinterGetStatusRequest()
        reply = self._invoke_bytes(_SERVICE, "GetStatus", request.SerializeToString())
        response = file_printer_pb2.FilePrinterStatus()
        response.ParseFromString(reply)
        return _status_from_proto(response)

    def set_online(self, online: bool) -> FilePrinterStatus:
        """Switch the printer online or offline.

        Offline, the receiver holds the byte the BBC strobed and withholds
        /ACK, so the guest waits as it would for a switched-off printer;
        online again, it accepts the byte and the print resumes. Returns the
        status after the change. Never blocks.
        """
        request = file_printer_pb2.FilePrinterSetOnlineRequest(online=online)
        reply = self._invoke_bytes(_SERVICE, "SetOnline", request.SerializeToString())
        response = file_printer_pb2.FilePrinterStatus()
        response.ParseFromString(reply)
        return _status_from_proto(response)


def _status_from_proto(response: file_printer_pb2.FilePrinterStatus) -> FilePrinterStatus:
    return FilePrinterStatus(
        path=response.path,
        bytes_received=response.bytes_received,
        bytes_written=response.bytes_written,
        online=response.online,
    )

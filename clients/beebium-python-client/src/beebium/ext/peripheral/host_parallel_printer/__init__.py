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

"""Client for the host-parallel-printer extension's typed API.

The host-parallel-printer extension sends everything the BBC prints to a
printer device on the host: /dev/usb/lp0 or /dev/lp0 on Linux, or on
Windows a USB printer's interface path or LPTn. This client reads the
device, whether it is open and how much has been written -- the
scripting-friendly equivalent of the sidebar panel -- and lists the printer
devices present, to help choose the ``device`` parameter. Requires the
server to be launched with ``--host-parallel-printer device=<path>``.

There is no way to switch the printer online or offline: it is online
exactly while its device is open.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client.extension import PeripheralExtensionAdapter
from beebium.ext.peripheral.host_parallel_printer._proto import host_parallel_printer_pb2

# The logical service name the host-parallel-printer extension's dispatcher
# registers (matches HostParallelPrinterDispatcher::service_name()).
_SERVICE = "HostParallelPrinter"


@dataclass(frozen=True)
class HostParallelPrinterStatus:
    """The device, whether it is open, and the byte counts."""

    device: str  # the host device path, as configured
    open: bool  # open, and so online
    bytes_received: int  # bytes accepted from the port since launch
    bytes_sent: int  # bytes written to the device


@dataclass(frozen=True)
class HostPrinterDevice:
    """A printer device present on the host."""

    path: str  # what to give as device=
    name: str  # a friendly name


class HostParallelPrinter(PeripheralExtensionAdapter):
    """Read the host printer's status and list the host's printer devices.

    The HostParallelPrinter messages are tunnelled over the core's
    ExtensionRpc channel.

    Usage:
        printer = bbc.extensions[HostParallelPrinter]
        printer.get_status().open
        printer.list_devices()
    """

    EXTENSION_NAME = "host-parallel-printer"

    def get_status(self) -> HostParallelPrinterStatus:
        """The device, whether it is open, and the byte counts."""
        request = host_parallel_printer_pb2.HostParallelPrinterGetStatusRequest()
        reply = self._invoke_bytes(_SERVICE, "GetStatus", request.SerializeToString())
        response = host_parallel_printer_pb2.HostParallelPrinterStatus()
        response.ParseFromString(reply)
        return HostParallelPrinterStatus(
            device=response.device,
            open=response.open,
            bytes_received=response.bytes_received,
            bytes_sent=response.bytes_sent,
        )

    def list_devices(self) -> list[HostPrinterDevice]:
        """The printer devices present on the host; empty when there are none.

        On Linux the /dev/usb/lp* and /dev/lp* nodes, on Windows the USB
        printer interface paths. Nothing is opened or chosen.
        """
        request = host_parallel_printer_pb2.HostParallelPrinterListDevicesRequest()
        reply = self._invoke_bytes(_SERVICE, "ListDevices", request.SerializeToString())
        response = host_parallel_printer_pb2.HostParallelPrinterDeviceList()
        response.ParseFromString(reply)
        return [HostPrinterDevice(path=d.path, name=d.name) for d in response.devices]

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

"""Piconet (USB-CDC Econet bridge) transport-specific operations.

These RPCs are served by the Piconet transport's PiconetService, tunnelled
over the core's ExtensionRpc channel, whenever the server has the Piconet
transport loaded. The current scope is intentionally
minimal -- just enough for a user to confirm "yes, my Piconet is
plugged in, and it's on /dev/X". Future Piconet RPCs (firmware
version, mode, counters, live clock detection via PollHardware,
SetMode, monitor-frame streaming) are documented in
src/extensions/piconet/piconet_service.proto.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client.extension import EconetTransportAdapter
from beebium.ext.econet.piconet._proto import piconet_service_pb2

# The logical service name the piconet extension's dispatcher registers
# (matches PiconetDispatcher::service_name() in the extension).
_SERVICE = "PiconetService"


@dataclass(frozen=True)
class PiconetStatus:
    """Piconet adapter status."""

    # Serial device path the Piconet is configured to use, e.g.
    # "/dev/tty.usbmodem101". Set at startup from the
    # --piconet device_path=... CLI argument or preset value.
    device_path: str

    # True if the SerialPort is currently open. On POSIX this is the
    # file descriptor's validity; if the user yanks the USB cable
    # mid-run this transitions to False. False at startup means the
    # device couldn't be opened.
    #
    # This says nothing about whether a real Econet wire is connected
    # to the Piconet adapter; only whether the host-side USB CDC
    # endpoint is responsive.
    serial_open: bool


class Piconet(EconetTransportAdapter):
    """Piconet-specific RPCs.

    Available whenever the server has the Piconet transport loaded. Check
    ``bbc.transport.active`` first (or use ``bbc.transport.get(Piconet)``)
    if your code might run against a server configured for AUN or no
    transport.

    Usage:
        piconet = bbc.transport[Piconet]         # or Piconet.attach(bbc)
        status = piconet.status
        print(f"Piconet on {status.device_path} (open={status.serial_open})")
    """

    EXTENSION_NAME = "piconet"

    @property
    def status(self) -> PiconetStatus:
        """Read the Piconet adapter status (device path + serial open).

        Tunnelled over the core's ExtensionRpc channel; the piconet extension
        no longer hosts its own gRPC service.
        """
        request = piconet_service_pb2.PiconetGetStatusRequest()
        reply = self._invoke_bytes(_SERVICE, "GetStatus", request.SerializeToString())
        response = piconet_service_pb2.PiconetGetStatusResponse()
        response.ParseFromString(reply)
        return PiconetStatus(
            device_path=response.device_path,
            serial_open=response.serial_open,
        )

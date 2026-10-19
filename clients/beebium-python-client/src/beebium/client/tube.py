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

"""Tube coprocessor management for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import tube_pb2, tube_pb2_grpc


@dataclass(frozen=True)
class TubeStatus:
    """The Tube and the coprocessor attached to it.

    A coprocessor is an in-process plugin attached at launch (``--tube-65c02``,
    ``--tube-65c102``, or a preset's coprocessor). When none is attached,
    ``coprocessor_connected`` is False and the coprocessor fields are empty or
    zero.
    """

    has_tube_socket: bool
    enabled: bool
    coprocessor_connected: bool
    coprocessor_cpu: str
    """The CPU as people name it ("65C02", "65C102"), as the
    ``{coprocessor-cpu}`` machine-description placeholder shows it."""
    coprocessor_clock_hz: int
    """The nominal clock in hertz (3000000 for the 6502 Second Processor)."""
    coprocessor_extension_id: str
    """The coprocessor extension's instance id, as the peripheral extension
    listing gives it."""


class Tube:
    """The Tube and its coprocessor.

    Usage:
        status = bbc.tube.status
        if status.coprocessor_connected:
            print(f"{status.coprocessor_cpu} at {status.coprocessor_clock_hz} Hz")

    The coprocessor itself is driven and debugged through
    :meth:`Beebium.connect_coprocessor`.
    """

    def __init__(self, stub: tube_pb2_grpc.TubeServiceStub):
        """Create a Tube interface.

        Args:
            stub: The gRPC stub for the TubeService.
        """
        self._stub = stub

    @property
    def status(self) -> TubeStatus:
        """Get current Tube status."""
        request = tube_pb2.GetTubeStatusRequest()
        response = self._stub.GetStatus(request)
        return TubeStatus(
            has_tube_socket=response.has_tube_socket,
            enabled=response.enabled,
            coprocessor_connected=response.coprocessor_connected,
            coprocessor_cpu=response.coprocessor_cpu,
            coprocessor_clock_hz=response.coprocessor_clock_hz,
            coprocessor_extension_id=response.coprocessor_extension_id,
        )

    @property
    def is_enabled(self) -> bool:
        """True if Tube hardware is currently enabled."""
        return self.status.enabled

    @property
    def coprocessor_connected(self) -> bool:
        """True if a coprocessor is attached to the Tube."""
        return self.status.coprocessor_connected

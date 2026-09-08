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
    """Current Tube coprocessor status."""

    has_tube_socket: bool
    enabled: bool
    coprocessor_connected: bool
    coprocessor_type: str
    coprocessor_clock_hz: int
    shared_memory_name: str
    coprocessor_grpc_address: str


class Tube:
    """Tube coprocessor management.

    Provides access to Tube status queries.

    Usage:
        # Check Tube status
        status = bbc.tube.status
        print(f"Enabled: {status.enabled}, Coprocessor: {status.coprocessor_type}")

        # Quick checks
        if bbc.tube.is_enabled:
            print(f"Coprocessor at: {bbc.tube.coprocessor_grpc_address}")
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
            coprocessor_type=response.coprocessor_type,
            coprocessor_clock_hz=response.coprocessor_clock_hz,
            shared_memory_name=response.shared_memory_name,
            coprocessor_grpc_address=response.coprocessor_grpc_address,
        )

    @property
    def is_enabled(self) -> bool:
        """True if Tube hardware is currently enabled."""
        return self.status.enabled

    @property
    def coprocessor_connected(self) -> bool:
        """True if a coprocessor process is connected."""
        return self.status.coprocessor_connected

    @property
    def coprocessor_grpc_address(self) -> str:
        """Coprocessor's gRPC address (empty if not registered)."""
        return self.status.coprocessor_grpc_address

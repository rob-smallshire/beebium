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

"""Hardware indicator (LED, motor activity) state access for the
Beebium client.

Indicators are named observable signals (``caps-lock-led``,
``shift-lock-led``, ``floppy-0-activity-led``, etc.) with values in
the range 0..255. Some indicators are PWM/duty-cycle filtered on the
server, so intermediate values can appear during fast transitions --
helpers like :meth:`Indicators.is_definitive_on` make those transients
explicit at the call site.
"""

from __future__ import annotations

from beebium.client._proto import indicator_pb2, indicator_pb2_grpc
from beebium.client._repr import facade_repr


class Indicators:
    """Client for the IndicatorService.

    Usage::

        # Read a single indicator
        brightness = bbc.indicators.get("caps-lock-led")
        if Indicators.is_definitive_on(brightness):
            ...

        # List registered indicators with metadata
        for info in bbc.indicators.list():
            print(info.name, info.metadata)
    """

    # Definitive on/off thresholds. Values strictly between are PWM-filter
    # transients and should not be acted on for state-comparison logic.
    DEFINITIVE_OFF = 0
    DEFINITIVE_ON = 255

    def __init__(self, stub: indicator_pb2_grpc.IndicatorServiceStub):
        """Create an Indicators interface.

        Args:
            stub: The gRPC stub for the IndicatorService.
        """
        self._stub = stub

    def __repr__(self) -> str:
        return facade_repr(self, "the machine's LEDs")

    def get_all(self) -> dict[str, int]:
        """Get the current value of every registered indicator.

        Returns:
            A mapping of indicator name to value (0..255).
        """
        request = indicator_pb2.GetIndicatorsRequest()
        response = self._stub.GetIndicators(request)
        return dict(response.values)

    def get(self, name: str) -> int:
        """Get the current value of a single indicator.

        Args:
            name: The indicator name (e.g., ``"caps-lock-led"``).

        Returns:
            The current value (0..255).

        Raises:
            KeyError: If no indicator with that name is registered.
        """
        values = self.get_all()
        if name not in values:
            raise KeyError(f"No indicator named {name!r}")
        return values[name]

    def list(self) -> list[indicator_pb2.IndicatorInfo]:
        """List all registered indicators with their metadata.

        Returns:
            A list of :class:`indicator_pb2.IndicatorInfo` entries.
        """
        request = indicator_pb2.ListIndicatorsRequest()
        response = self._stub.ListIndicators(request)
        return list(response.indicators)

    @staticmethod
    def is_definitive_on(value: int) -> bool:
        """True if ``value`` represents an unambiguous "on" reading.

        PWM/duty-cycle filtered indicators can produce intermediate
        values during transitions. Only 255 is treated as definitively
        on so callers don't act on transients.
        """
        return value == Indicators.DEFINITIVE_ON

    @staticmethod
    def is_definitive_off(value: int) -> bool:
        """True if ``value`` represents an unambiguous "off" reading."""
        return value == Indicators.DEFINITIVE_OFF

    @staticmethod
    def is_transient(value: int) -> bool:
        """True if ``value`` is between off and on (PWM-filter transient)."""
        return Indicators.DEFINITIVE_OFF < value < Indicators.DEFINITIVE_ON

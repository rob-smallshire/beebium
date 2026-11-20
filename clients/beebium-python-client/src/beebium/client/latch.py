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

"""IC32 Addressable Latch access for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from beebium.client._proto import debugger_pb2
from beebium.client._repr import facade_repr

if TYPE_CHECKING:
    from beebium.client._proto import debugger_pb2_grpc


@dataclass
class AddressableLatchState:
    """Addressable Latch (IC32 74LS259 on Model B) state snapshot.

    The latch is controlled by System VIA Port B and is write-only on hardware.
    This provides debugger access to its current state.
    """

    # Raw 8-bit latch value
    value: int

    # Decoded fields
    screen_base: int  # Bits 4-5: Screen memory base address select
    sound_write_enable: bool  # Bit 0: Active low
    speech_read: bool  # Bit 1: Active low
    speech_write: bool  # Bit 2: Active low
    keyboard_write: bool  # Bit 3: Active low
    caps_lock_led: bool  # Bit 6: Active low (True = LED on)
    shift_lock_led: bool  # Bit 7: Active low (True = LED on)

    # Bit definitions (matching hardware active-low conventions)
    BIT_SOUND_WRITE = 0x01
    BIT_SPEECH_READ = 0x02
    BIT_SPEECH_WRITE = 0x04
    BIT_KB_WRITE = 0x08
    BIT_SCREEN_BASE_LO = 0x10
    BIT_SCREEN_BASE_HI = 0x20
    BIT_CAPS_LOCK = 0x40
    BIT_SHIFT_LOCK = 0x80

    def __str__(self) -> str:
        """Format latch state for display."""
        leds = []
        if self.caps_lock_led:
            leds.append("CAPS")
        if self.shift_lock_led:
            leds.append("SHIFT")
        led_str = ", ".join(leds) if leds else "none"

        return (
            f"Latch={self.value:02X} ScreenBase={self.screen_base}\n"
            f"Sound={self.sound_write_enable} KB={self.keyboard_write}\n"
            f"LEDs: {led_str}"
        )


class AddressableLatch:
    """Addressable Latch access.

    Provides read access to the addressable latch state, which is
    write-only on actual hardware.

    Usage:
        # Get latch state
        state = bbc.addressable_latch.state
        print(f"Caps Lock LED: {state.caps_lock_led}")
        print(f"Screen base: {state.screen_base}")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub):
        """Create an AddressableLatch interface.

        Args:
            stub: The gRPC stub for the DeviceInspection service.
        """
        self._stub = stub

    def __repr__(self) -> str:
        return facade_repr(self, "the addressable latch on the System VIA")

    @property
    def state(self) -> AddressableLatchState:
        """Get the current latch state."""
        request = debugger_pb2.GetAddressableLatchStateRequest()
        response = self._stub.GetAddressableLatchState(request)

        return AddressableLatchState(
            value=response.value,
            screen_base=response.screen_base,
            sound_write_enable=response.sound_write_enable,
            speech_read=response.speech_read,
            speech_write=response.speech_write,
            keyboard_write=response.keyboard_write,
            caps_lock_led=response.caps_lock_led,
            shift_lock_led=response.shift_lock_led,
        )

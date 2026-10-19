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

"""The machine's own real-time clock chip, read without side effects."""

from __future__ import annotations

from dataclasses import dataclass

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc

# The first register of the chip's battery-backed CMOS RAM.
FIRST_CMOS_REGISTER = 0x0E


@dataclass(frozen=True)
class RealTimeClockState:
    """The clock chip's registers, as a read of each would return them.

    ``present`` is False on a machine with no clock of its own (only the
    Integra-B has one, a CDP6818); the other fields are then empty.
    """

    present: bool
    chip: str
    registers: bytes
    """All 64 registers: 0-9 the time and alarm, 10-13 control registers A-D,
    14-63 the battery-backed CMOS RAM."""
    address_register: int
    """The register the chip's address latch selects."""

    @property
    def cmos_ram(self) -> bytes:
        """The battery-backed CMOS RAM, registers 14-63. On the Integra-B,
        IBOS's user register N is ``cmos_ram[N]``."""
        return self.registers[FIRST_CMOS_REGISTER:]


class RealTimeClock:
    """The machine's own real-time clock chip.

    Reading through the guest's RTC ports has side effects -- it moves the
    chip's address latch, and a read of register C clears its interrupt
    flags -- so a test that wants the raw registers reads them here instead.
    A plug-in clock (acorn-rtc on the user port) is an extension and is not
    reported.

    Usage::

        state = bbc.real_time_clock.state
        if state.present:
            print(f"IBOS user register &0F: &{state.cmos_ram[0x0F]:02X}")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub):
        self._stub = stub

    @property
    def state(self) -> RealTimeClockState:
        """The clock's registers, read without side effects."""
        response = self._stub.GetRealTimeClockState(debugger_pb2.GetRealTimeClockStateRequest())
        return RealTimeClockState(
            present=response.present,
            chip=response.chip,
            registers=bytes(response.registers),
            address_register=response.address_register,
        )

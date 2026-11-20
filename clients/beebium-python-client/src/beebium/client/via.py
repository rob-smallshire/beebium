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

"""6522 VIA (Versatile Interface Adapter) access for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import TYPE_CHECKING

from beebium.client._proto import debugger_pb2
from beebium.client._repr import facade_repr

if TYPE_CHECKING:
    from beebium.client._proto import debugger_pb2_grpc


class ViaId(Enum):
    """VIA identifier for selecting System or User VIA."""

    SYSTEM = "system"
    USER = "user"


@dataclass
class ViaState:
    """6522 VIA state snapshot.

    Includes all register values and internal state for debugging.
    """

    # Port registers
    ora: int  # Output Register A
    orb: int  # Output Register B
    ddra: int  # Data Direction Register A
    ddrb: int  # Data Direction Register B
    ira: int  # Input Register A (computed from port pins)
    irb: int  # Input Register B (computed from port pins)

    # Timer 1
    t1c: int  # Timer 1 counter (16-bit effective value)
    t1l: int  # Timer 1 latch (16-bit)

    # Timer 2
    t2c: int  # Timer 2 counter (16-bit effective value)
    t2l: int  # Timer 2 latch (8-bit, low byte only)

    # Control registers
    acr: int  # Auxiliary Control Register
    pcr: int  # Peripheral Control Register
    sr: int  # Shift Register

    # Interrupt registers
    ifr: int  # Interrupt Flag Register
    ier: int  # Interrupt Enable Register

    # Internal state
    t1_pending: bool  # Timer 1 active and can generate IRQ
    t2_pending: bool  # Timer 2 active and can generate IRQ
    t1_pb7: int  # PB7 output state from Timer 1

    # Control lines
    ca1: bool
    ca2: bool
    cb1: bool
    cb2: bool

    # ACR flag accessors

    @property
    def pa_latching(self) -> bool:
        """Port A input latching enabled on CA1 edge."""
        return bool(self.acr & 0x01)

    @property
    def pb_latching(self) -> bool:
        """Port B input latching enabled on CB1 edge."""
        return bool(self.acr & 0x02)

    @property
    def t2_count_pb6(self) -> bool:
        """Timer 2 counts PB6 pulses (else counts phi2)."""
        return bool(self.acr & 0x20)

    @property
    def t1_continuous(self) -> bool:
        """Timer 1 free-running mode (else one-shot)."""
        return bool(self.acr & 0x40)

    @property
    def t1_output_pb7(self) -> bool:
        """Timer 1 toggles PB7 output on timeout."""
        return bool(self.acr & 0x80)

    # IFR flag accessors

    @property
    def ifr_ca2(self) -> bool:
        """CA2 interrupt flag."""
        return bool(self.ifr & 0x01)

    @property
    def ifr_ca1(self) -> bool:
        """CA1 interrupt flag."""
        return bool(self.ifr & 0x02)

    @property
    def ifr_sr(self) -> bool:
        """Shift register interrupt flag."""
        return bool(self.ifr & 0x04)

    @property
    def ifr_cb2(self) -> bool:
        """CB2 interrupt flag."""
        return bool(self.ifr & 0x08)

    @property
    def ifr_cb1(self) -> bool:
        """CB1 interrupt flag."""
        return bool(self.ifr & 0x10)

    @property
    def ifr_t2(self) -> bool:
        """Timer 2 interrupt flag."""
        return bool(self.ifr & 0x20)

    @property
    def ifr_t1(self) -> bool:
        """Timer 1 interrupt flag."""
        return bool(self.ifr & 0x40)

    @property
    def ifr_irq(self) -> bool:
        """Master IRQ flag (set when any enabled interrupt is pending)."""
        return bool(self.ifr & 0x80)

    def __str__(self) -> str:
        """Format VIA state for display."""
        return (
            f"ORA={self.ora:02X} ORB={self.orb:02X} "
            f"DDRA={self.ddra:02X} DDRB={self.ddrb:02X}\n"
            f"T1C={self.t1c:04X} T1L={self.t1l:04X} "
            f"T2C={self.t2c:04X} T2L={self.t2l:02X}\n"
            f"ACR={self.acr:02X} PCR={self.pcr:02X} SR={self.sr:02X}\n"
            f"IFR={self.ifr:02X} IER={self.ier:02X}"
        )


class Via:
    """6522 VIA access for System VIA or User VIA.

    Provides read access to VIA state including registers and internal state.

    Usage:
        # Get system VIA state
        state = bbc.system_via.state
        print(f"T1 counter: {state.t1c:04X}")

        # Check timer status
        if bbc.system_via.state.t1_pending:
            print("Timer 1 is running")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub, via_id: ViaId):
        """Create a VIA interface.

        Args:
            stub: The gRPC stub for the DeviceInspection service.
            via_id: Which VIA to access (SYSTEM or USER).
        """
        self._stub = stub
        self._via_id = via_id

    def __repr__(self) -> str:
        return facade_repr(self, f"the {self._via_id.value.capitalize()} VIA (6522)")

    @property
    def state(self) -> ViaState:
        """Get the current VIA state."""
        if self._via_id == ViaId.SYSTEM:
            request = debugger_pb2.GetSystemViaStateRequest()
            response = self._stub.GetSystemViaState(request)
        else:
            request = debugger_pb2.GetUserViaStateRequest()
            response = self._stub.GetUserViaState(request)

        return ViaState(
            ora=response.ora,
            orb=response.orb,
            ddra=response.ddra,
            ddrb=response.ddrb,
            ira=response.ira,
            irb=response.irb,
            t1c=response.t1c,
            t1l=response.t1l,
            t2c=response.t2c,
            t2l=response.t2l,
            acr=response.acr,
            pcr=response.pcr,
            sr=response.sr,
            ifr=response.ifr,
            ier=response.ier,
            t1_pending=response.t1_pending,
            t2_pending=response.t2_pending,
            t1_pb7=response.t1_pb7,
            ca1=response.ca1,
            ca2=response.ca2,
            cb1=response.cb1,
            cb2=response.cb2,
        )

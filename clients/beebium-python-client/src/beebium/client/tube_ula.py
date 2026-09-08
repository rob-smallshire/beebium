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

"""Tube ULA device inspection for the beebium client."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING

from beebium.client._proto import debugger_pb2

if TYPE_CHECKING:
    from beebium.client._proto import debugger_pb2_grpc


@dataclass
class TubeControlFlags:
    """Tube ULA control flags (6 bits, written by host)."""

    q: bool = False  # Bit 0: Enable HIRQ from R4
    i: bool = False  # Bit 1: Enable PIRQ from R1
    j: bool = False  # Bit 2: Enable PIRQ from R4
    m: bool = False  # Bit 3: Enable PNMI from R3
    v: bool = False  # Bit 4: Two-byte mode for R3
    p: bool = False  # Bit 5: Coprocessor reset

    @property
    def raw(self) -> int:
        """Raw 6-bit value."""
        return (self.q << 0) | (self.i << 1) | (self.j << 2) | (self.m << 3) | (self.v << 4) | (self.p << 5)

    def __str__(self) -> str:
        flags = []
        if self.q:
            flags.append("Q")
        if self.i:
            flags.append("I")
        if self.j:
            flags.append("J")
        if self.m:
            flags.append("M")
        if self.v:
            flags.append("V")
        if self.p:
            flags.append("P")
        return f"Flags={self.raw:02X} [{' '.join(flags) or 'none'}]"


@dataclass
class TubeLatchState:
    """Single-byte latch state."""

    value: int = 0
    data_available: bool = False

    def __str__(self) -> str:
        avail = "AVAIL" if self.data_available else "empty"
        return f"${self.value:02X} ({avail})"


@dataclass
class TubeFifo24State:
    """24-byte FIFO state (R1 P-to-H)."""

    count: int = 0
    data: bytes = b""
    data_available: bool = False

    def __str__(self) -> str:
        avail = "AVAIL" if self.data_available else "empty"
        hex_data = " ".join(f"{b:02X}" for b in self.data[:8])
        if len(self.data) > 8:
            hex_data += " ..."
        return f"count={self.count}/24 ({avail}) [{hex_data}]"


@dataclass
class TubeReg3State:
    """2-byte shift register state (R3)."""

    count: int = 0
    data: bytes = b""
    pending: bool = False
    threshold: int = 1

    def __str__(self) -> str:
        mode = "2-byte" if self.threshold == 2 else "1-byte"
        pend = "PENDING" if self.pending else "idle"
        hex_data = " ".join(f"{b:02X}" for b in self.data)
        return f"count={self.count}/{self.threshold} ({pend}, {mode}) [{hex_data}]"


@dataclass
class TubeHostStatus:
    """Host-perspective status registers."""

    r1_status: int = 0
    r2_status: int = 0
    r3_status: int = 0
    r4_status: int = 0

    def __str__(self) -> str:
        return f"R1=${self.r1_status:02X} R2=${self.r2_status:02X} R3=${self.r3_status:02X} R4=${self.r4_status:02X}"


@dataclass
class TubeCoprocessorStatus:
    """Coprocessor-perspective status registers."""

    r1_status: int = 0
    r2_status: int = 0
    r3_status: int = 0
    r4_status: int = 0

    def __str__(self) -> str:
        return f"R1=${self.r1_status:02X} R2=${self.r2_status:02X} R3=${self.r3_status:02X} R4=${self.r4_status:02X}"


@dataclass
class TubeInterrupts:
    """Tube interrupt output state."""

    hirq: bool = False
    pirq: bool = False
    pnmi_level: bool = False
    pnmi_edge: bool = False

    def __str__(self) -> str:
        active = []
        if self.hirq:
            active.append("HIRQ")
        if self.pirq:
            active.append("PIRQ")
        if self.pnmi_level:
            active.append("PNMI-level")
        if self.pnmi_edge:
            active.append("PNMI-edge")
        return " ".join(active) or "none"


@dataclass
class TubeTransferCounters:
    """Per-register byte transfer counters for Tube diagnostics."""

    r1_h2p_writes: int = 0
    r1_h2p_reads: int = 0
    r2_h2p_writes: int = 0
    r2_h2p_reads: int = 0
    r3_h2p_writes: int = 0
    r3_h2p_reads: int = 0
    r4_h2p_writes: int = 0
    r4_h2p_reads: int = 0

    r1_p2h_writes: int = 0
    r1_p2h_reads: int = 0
    r2_p2h_writes: int = 0
    r2_p2h_reads: int = 0
    r3_p2h_writes: int = 0
    r3_p2h_reads: int = 0
    r4_p2h_writes: int = 0
    r4_p2h_reads: int = 0

    def __str__(self) -> str:
        lines = ["Transfer counters:"]
        for reg in range(1, 5):
            h2p_w = getattr(self, f"r{reg}_h2p_writes")
            h2p_r = getattr(self, f"r{reg}_h2p_reads")
            p2h_w = getattr(self, f"r{reg}_p2h_writes")
            p2h_r = getattr(self, f"r{reg}_p2h_reads")
            h2p_delta = h2p_w - h2p_r
            p2h_delta = p2h_w - p2h_r
            lines.append(
                f"  R{reg}: H->P w={h2p_w} r={h2p_r} (delta={h2p_delta})  P->H w={p2h_w} r={p2h_r} (delta={p2h_delta})"
            )
        return "\n".join(lines)


@dataclass
class TubeUlaState:
    """Complete Tube ULA state snapshot."""

    enabled: bool = False
    control_flags: TubeControlFlags = field(default_factory=TubeControlFlags)

    r1_h2p: TubeLatchState = field(default_factory=TubeLatchState)
    r1_p2h: TubeFifo24State = field(default_factory=TubeFifo24State)
    r2_h2p: TubeLatchState = field(default_factory=TubeLatchState)
    r2_p2h: TubeLatchState = field(default_factory=TubeLatchState)
    r3_h2p: TubeReg3State = field(default_factory=TubeReg3State)
    r3_p2h: TubeReg3State = field(default_factory=TubeReg3State)
    r4_h2p: TubeLatchState = field(default_factory=TubeLatchState)
    r4_p2h: TubeLatchState = field(default_factory=TubeLatchState)

    host_status: TubeHostStatus = field(default_factory=TubeHostStatus)
    coprocessor_status: TubeCoprocessorStatus = field(default_factory=TubeCoprocessorStatus)
    interrupts: TubeInterrupts = field(default_factory=TubeInterrupts)

    host_stretched: bool = False
    counters: TubeTransferCounters = field(default_factory=TubeTransferCounters)

    def __str__(self) -> str:
        if not self.enabled:
            return "Tube: not enabled"
        lines = [
            f"Tube ULA: {self.control_flags}",
            f"  R1 H->P: {self.r1_h2p}",
            f"  R1 P->H: {self.r1_p2h}",
            f"  R2 H->P: {self.r2_h2p}",
            f"  R2 P->H: {self.r2_p2h}",
            f"  R3 H->P: {self.r3_h2p}",
            f"  R3 P->H: {self.r3_p2h}",
            f"  R4 H->P: {self.r4_h2p}",
            f"  R4 P->H: {self.r4_p2h}",
            f"  Host status:     {self.host_status}",
            f"  Coprocessor status: {self.coprocessor_status}",
            f"  Interrupts: {self.interrupts}",
        ]
        if self.host_stretched:
            lines.append("  Host: STRETCHED")
        lines.append(f"  {self.counters}")
        return "\n".join(lines)


class TubeUlaInspection:
    """Tube ULA device inspection.

    Provides side-effect-free access to Tube ULA register state for debugging.

    Usage:
        state = bbc.tube_ula.state
        print(state)  # Full formatted dump
        print(f"R1 H->P: {state.r1_h2p}")
        if state.interrupts.hirq:
            print("Host IRQ active")
    """

    def __init__(self, stub: debugger_pb2_grpc.DeviceInspectionStub):
        self._stub = stub

    @property
    def state(self) -> TubeUlaState:
        """Get the current Tube ULA state."""
        request = debugger_pb2.GetTubeStateRequest()
        response = self._stub.GetTubeState(request)
        return _from_proto(response)


def _from_proto(pb: debugger_pb2.TubeState) -> TubeUlaState:
    """Convert a protobuf TubeState to a TubeUlaState dataclass."""
    cf = pb.control_flags
    return TubeUlaState(
        enabled=pb.enabled,
        control_flags=TubeControlFlags(
            q=cf.q,
            i=cf.i,
            j=cf.j,
            m=cf.m,
            v=cf.v,
            p=cf.p,
        ),
        r1_h2p=TubeLatchState(
            value=pb.r1_h2p.value,
            data_available=pb.r1_h2p.data_available,
        ),
        r1_p2h=TubeFifo24State(
            count=pb.r1_p2h.count,
            data=pb.r1_p2h.data,
            data_available=pb.r1_p2h.data_available,
        ),
        r2_h2p=TubeLatchState(
            value=pb.r2_h2p.value,
            data_available=pb.r2_h2p.data_available,
        ),
        r2_p2h=TubeLatchState(
            value=pb.r2_p2h.value,
            data_available=pb.r2_p2h.data_available,
        ),
        r3_h2p=TubeReg3State(
            count=pb.r3_h2p.count,
            data=pb.r3_h2p.data,
            pending=pb.r3_h2p.pending,
            threshold=pb.r3_h2p.threshold,
        ),
        r3_p2h=TubeReg3State(
            count=pb.r3_p2h.count,
            data=pb.r3_p2h.data,
            pending=pb.r3_p2h.pending,
            threshold=pb.r3_p2h.threshold,
        ),
        r4_h2p=TubeLatchState(
            value=pb.r4_h2p.value,
            data_available=pb.r4_h2p.data_available,
        ),
        r4_p2h=TubeLatchState(
            value=pb.r4_p2h.value,
            data_available=pb.r4_p2h.data_available,
        ),
        host_status=TubeHostStatus(
            r1_status=pb.host_status.r1_status,
            r2_status=pb.host_status.r2_status,
            r3_status=pb.host_status.r3_status,
            r4_status=pb.host_status.r4_status,
        ),
        coprocessor_status=TubeCoprocessorStatus(
            r1_status=pb.coprocessor_status.r1_status,
            r2_status=pb.coprocessor_status.r2_status,
            r3_status=pb.coprocessor_status.r3_status,
            r4_status=pb.coprocessor_status.r4_status,
        ),
        interrupts=TubeInterrupts(
            hirq=pb.interrupts.hirq,
            pirq=pb.interrupts.pirq,
            pnmi_level=pb.interrupts.pnmi_level,
            pnmi_edge=pb.interrupts.pnmi_edge,
        ),
        host_stretched=pb.host_stretched,
        counters=TubeTransferCounters(
            r1_h2p_writes=pb.counters.r1_h2p_writes,
            r1_h2p_reads=pb.counters.r1_h2p_reads,
            r2_h2p_writes=pb.counters.r2_h2p_writes,
            r2_h2p_reads=pb.counters.r2_h2p_reads,
            r3_h2p_writes=pb.counters.r3_h2p_writes,
            r3_h2p_reads=pb.counters.r3_h2p_reads,
            r4_h2p_writes=pb.counters.r4_h2p_writes,
            r4_h2p_reads=pb.counters.r4_h2p_reads,
            r1_p2h_writes=pb.counters.r1_p2h_writes,
            r1_p2h_reads=pb.counters.r1_p2h_reads,
            r2_p2h_writes=pb.counters.r2_p2h_writes,
            r2_p2h_reads=pb.counters.r2_p2h_reads,
            r3_p2h_writes=pb.counters.r3_p2h_writes,
            r3_p2h_reads=pb.counters.r3_p2h_reads,
            r4_p2h_writes=pb.counters.r4_p2h_writes,
            r4_p2h_reads=pb.counters.r4_p2h_reads,
        ),
    )

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

"""CPU register access for the beebium client.

The CPU describes its own registers and interrupt signals, so this layer names
no CPU family. ``cpu.registers`` is an ordered mapping of register name to
value, also reachable as lowercase attributes (``regs.a``, ``regs.pc``, and a
Z80's ``regs.hl``), built from ``cpu.descriptor``. The disassembler stays
6502-only; only the register model is family-agnostic.
"""

from __future__ import annotations

from collections.abc import Iterator, Mapping
from dataclasses import dataclass

from beebium.client._proto import debugger_pb2, debugger_pb2_grpc


class StatusRegister:
    """A CPU flags register decoded by name, built from the descriptor.

    An immutable value object wrapping the raw flags byte and the flag names
    the descriptor gives for it (bit 0 first, "" for an unused bit). Flags are
    read by name -- ``status.flag("C")`` or ``status["C"]`` -- and rendered by
    name. For a 6502 the familiar property names (``carry``, ``zero``,
    ``interrupt_disable``, ``decimal``, ``break_flag``, ``overflow``,
    ``negative``) are available as aliases, but only when the descriptor carries
    the matching flag; on a CPU whose flags register uses other names they are
    absent rather than misleading.
    """

    # 6502 property name -> descriptor flag name.
    _ALIASES = {
        "carry": "C",
        "zero": "Z",
        "interrupt_disable": "I",
        "decimal": "D",
        "break_flag": "B",
        "overflow": "V",
        "negative": "N",
    }

    def __init__(self, value: int, flag_names: list[str] | None = None):
        self._value = value
        # Default to the 6502 layout so StatusRegister(byte) keeps working.
        names = flag_names if flag_names is not None else ["C", "Z", "I", "D", "B", "", "V", "N"]
        self._flag_names = list(names)
        self._bit_of = {name: i for i, name in enumerate(self._flag_names) if name}

    @property
    def value(self) -> int:
        """The raw flags byte."""
        return self._value

    def flag(self, name: str) -> bool:
        """The flag of the given descriptor name (e.g. "C", "N")."""
        return bool(self._value & (1 << self._bit_of[name]))

    def __getitem__(self, name: str) -> bool:
        return self.flag(name)

    def __contains__(self, name: str) -> bool:
        return name in self._bit_of

    def __getattr__(self, name: str) -> bool:
        # 6502 property aliases, present only when the descriptor has the flag.
        aliases = type(self)._ALIASES
        if name in aliases and aliases[name] in self.__dict__["_bit_of"]:
            return self.flag(aliases[name])
        raise AttributeError(f"{type(self).__name__!r} has no flag {name!r}")

    def __int__(self) -> int:
        return self._value

    def __str__(self) -> str:
        """Render flags by name, MSB first: set uppercase, clear lowercase.

        An unused bit renders as "-". Single-letter 6502 names give the
        conventional string (e.g. "Nv-bdiZC").
        """
        chars = []
        for bit in range(len(self._flag_names) - 1, -1, -1):
            name = self._flag_names[bit]
            if not name:
                chars.append("-")
            elif self._value & (1 << bit):
                chars.append(name.upper())
            else:
                chars.append(name.lower())
        return "".join(chars)


@dataclass(frozen=True)
class Signal:
    """The state of one CPU interrupt line, e.g. IRQ or NMI."""

    name: str
    asserted: bool = False
    pending: bool = False
    in_handler: bool = False

    def __str__(self) -> str:
        flags = [
            label
            for label, on in (
                ("asserted", self.asserted),
                ("pending", self.pending),
                ("in-handler", self.in_handler),
            )
            if on
        ]
        return f"{self.name}({', '.join(flags)})" if flags else self.name


class Registers(Mapping):
    """An immutable snapshot of the CPU registers, built from the descriptor.

    An ordered mapping of register name (as the descriptor names it, e.g. "A",
    "PC", "HL") to value, so ``regs["PC"]`` works for any CPU. Each register is
    also a lowercase attribute -- ``regs.a``, ``regs.pc`` -- so existing 6502
    code keeps working and a new family's registers appear with no client
    change. Writes never mutate a snapshot; they go through ``cpu.update(...)``.
    """

    def __init__(self, descriptor: debugger_pb2.CpuDescriptor, values: dict[str, int]):
        # Preserve descriptor order.
        self._descriptor = descriptor
        self._values = dict(values)
        self._by_lower = {name.lower(): name for name in values}

    def __getitem__(self, name: str) -> int:
        return self._values[name]

    def __iter__(self) -> Iterator[str]:
        return iter(self._values)

    def __len__(self) -> int:
        return len(self._values)

    def __getattr__(self, name: str) -> int:
        # Lowercase attribute access, e.g. regs.pc -> the "PC" register.
        try:
            canonical = self.__dict__["_by_lower"][name]
        except KeyError:
            raise AttributeError(
                f"{type(self).__name__!r} has no register {name!r}"
            ) from None
        return self.__dict__["_values"][canonical]

    @property
    def status(self) -> StatusRegister:
        """The flags register decoded into named 6502 flags.

        Built from the register the descriptor marks as the FLAGS register.
        """
        for reg in self._descriptor.registers:
            if reg.role == debugger_pb2.FLAGS:
                return StatusRegister(self._values[reg.name], list(reg.flag_names))
        raise AttributeError("this CPU has no flags register")

    def __str__(self) -> str:
        parts = []
        for reg in self._descriptor.registers:
            width = max(1, (reg.width_bits + 3) // 4)
            parts.append(f"{reg.name}={self._values[reg.name]:0{width}X}")
        text = " ".join(parts)
        for reg in self._descriptor.registers:
            if reg.role == debugger_pb2.FLAGS:
                text += f" [{StatusRegister(self._values[reg.name], list(reg.flag_names))}]"
                break
        return text


def _registers_from_state(
    descriptor: debugger_pb2.CpuDescriptor, state: debugger_pb2.CpuState
) -> Registers:
    """Build a Registers snapshot from a descriptor and a CpuState proto."""
    values = {rv.name: rv.value for rv in state.registers}
    return Registers(descriptor, values)


class CPU:
    """CPU register access.

    Reads return a coherent snapshot; writes are atomic and return the
    resulting snapshot.

    Usage:
        # Read all registers as one coherent snapshot (one request)
        regs = bbc.cpu.registers
        print(regs)                     # A=.. X=.. ... PC=.. P=.. [flags]
        if regs.status.carry:
            ...
        pc = regs["PC"]                 # by name, for any CPU

        # Convenience single-register access (each read is its own snapshot)
        if bbc.cpu.a == 0:
            ...

        # Atomic partial write; returns the complete new register state
        new = bbc.cpu.update(pc=0xC000, a=0x42)

        # The individual setters route through update()
        bbc.cpu.pc = 0xC000

        # Describe the CPU, or read its interrupt lines
        bbc.cpu.descriptor.family          # "6502"
        bbc.cpu.signals["NMI"].pending
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a CPU interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub
        self._descriptor: debugger_pb2.CpuDescriptor | None = None

    @property
    def descriptor(self) -> debugger_pb2.CpuDescriptor:
        """The CPU's self-description, fetched once and cached.

        Lists the registers in display order (each with a name, width, role and,
        for the flags register, per-bit flag names) and the interrupt signals.
        """
        if self._descriptor is None:
            self._descriptor = self._stub.GetCpuDescriptor(debugger_pb2.Empty())
        return self._descriptor

    @property
    def registers(self) -> Registers:
        """Read all registers as one coherent snapshot."""
        state = self._stub.GetCpuState(debugger_pb2.Empty())
        return _registers_from_state(self.descriptor, state)

    @property
    def signals(self) -> dict[str, Signal]:
        """The CPU's interrupt lines and their current state, keyed by name."""
        state = self._stub.GetCpuState(debugger_pb2.Empty())
        return {
            ss.name: Signal(
                name=ss.name,
                asserted=ss.asserted,
                pending=ss.pending,
                in_handler=ss.in_handler,
            )
            for ss in state.signals
        }

    # Individual register properties (read)

    @property
    def a(self) -> int:
        """Accumulator (0-255)."""
        return self.registers.a

    @property
    def x(self) -> int:
        """X index register (0-255)."""
        return self.registers.x

    @property
    def y(self) -> int:
        """Y index register (0-255)."""
        return self.registers.y

    @property
    def sp(self) -> int:
        """Stack pointer (0-255)."""
        return self.registers.sp

    @property
    def pc(self) -> int:
        """Program counter (0-65535)."""
        return self.registers.pc

    @property
    def p(self) -> int:
        """Processor status flags (0-255)."""
        return self.registers.p

    # Individual register setters

    @a.setter
    def a(self, value: int) -> None:
        self.update(a=value)

    @x.setter
    def x(self, value: int) -> None:
        self.update(x=value)

    @y.setter
    def y(self, value: int) -> None:
        self.update(y=value)

    @sp.setter
    def sp(self, value: int) -> None:
        self.update(sp=value)

    @pc.setter
    def pc(self, value: int) -> None:
        self.update(pc=value)

    @p.setter
    def p(self, value: int) -> None:
        self.update(p=value)

    def update(self, **values: int) -> Registers:
        """Atomically write one or more registers and return the new snapshot.

        Registers are named as lowercase keyword arguments (``a=``, ``pc=``, and
        a Z80's ``hl=``). Only the registers provided are modified; the rest are
        left unchanged. The server applies the writes and reads back the
        resulting state as a single operation, so the returned ``Registers`` is a
        coherent post-write snapshot -- there is no separate read and no race.
        An unknown register name is rejected by the server, naming it.
        """
        by_lower = {reg.name.lower(): reg.name for reg in self.descriptor.registers}
        request = debugger_pb2.CpuState()
        for key, value in values.items():
            rv = request.registers.add()
            # Pass the descriptor's canonical name when we know it, else the key
            # as given, so the server's unknown-name rejection reports it.
            rv.name = by_lower.get(key.lower(), key)
            rv.value = value
        state = self._stub.SetCpuState(request)
        return _registers_from_state(self.descriptor, state)

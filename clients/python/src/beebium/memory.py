# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""Memory access for the beebium client.

This module provides a Pythonic interface for reading and writing memory in the
emulated BBC Micro. Memory access is explicit about side effects:

- `bbc.memory.bus` - side-effecting access (goes through memory bus like real hardware)
- `bbc.memory.peek` - side-effect-free access (read-only)

The `cast(fmt)` method provides typed access using struct format strings.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import overload

from beebium._proto import debugger_pb2, debugger_pb2_grpc
from beebium.exceptions import MemoryAccessError


class MemoryAccessorBase:
    """Common base for memory accessors."""

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        self._stub = stub

    def _read_bytes(self, address: int, length: int) -> bytes:
        """Read bytes from memory. Subclasses implement gRPC call."""
        raise NotImplementedError

    def _write_bytes(self, address: int, data: bytes) -> None:
        """Write bytes to memory. Subclasses implement gRPC call."""
        raise NotImplementedError


class TypedMemoryReader:
    """Typed read-only memory access using struct format."""

    def __init__(self, accessor: MemoryReader, fmt: str):
        self._accessor = accessor
        self._fmt = fmt
        self._size = struct.calcsize(fmt)

    @overload
    def __getitem__(self, address: int) -> int: ...

    @overload
    def __getitem__(self, address: slice) -> tuple[int, ...]: ...

    def __getitem__(self, address: int | slice) -> int | tuple[int, ...]:
        if isinstance(address, int):
            data = self._accessor.read(address, self._size)
            return struct.unpack(self._fmt, data)[0]
        if isinstance(address, slice):
            start = address.start or 0
            stop = address.stop
            if stop is None:
                raise ValueError("slice must have an explicit stop address")
            length = stop - start
            if length % self._size != 0:
                raise ValueError(
                    f"slice length {length} not a multiple of {self._size}"
                )
            data = self._accessor.read(start, length)
            count = length // self._size
            return struct.unpack(f"<{count}{self._fmt[1:]}", data)
        raise TypeError(f"indices must be int or slice, not {type(address).__name__}")


class TypedMemoryAccessor(TypedMemoryReader):
    """Typed read+write memory access using struct format."""

    def __init__(self, accessor: BusMemoryAccessor, fmt: str):
        super().__init__(accessor, fmt)
        self._bus_accessor = accessor

    @overload
    def __setitem__(self, address: int, value: int) -> None: ...

    @overload
    def __setitem__(self, address: slice, value: tuple[int, ...]) -> None: ...

    def __setitem__(self, address: int | slice, value: int | tuple[int, ...]) -> None:
        if isinstance(address, int):
            data = struct.pack(self._fmt, value)
            self._bus_accessor.write(address, data)
        elif isinstance(address, slice):
            start = address.start or 0
            stop = address.stop
            if stop is None:
                raise ValueError("slice must have an explicit stop address")
            length = stop - start
            if length % self._size != 0:
                raise ValueError(
                    f"slice length {length} not a multiple of {self._size}"
                )
            expected_count = length // self._size
            if not hasattr(value, "__len__") or len(value) != expected_count:
                raise ValueError(f"expected {expected_count} values, got {len(value) if hasattr(value, '__len__') else 1}")
            data = struct.pack(f"<{expected_count}{self._fmt[1:]}", *value)
            self._bus_accessor.write(start, data)
        else:
            raise TypeError(f"indices must be int or slice, not {type(address).__name__}")


class MemoryReader(MemoryAccessorBase):
    """Read-only memory operations."""

    @overload
    def __getitem__(self, address: int) -> int: ...

    @overload
    def __getitem__(self, address: slice) -> bytes: ...

    def __getitem__(self, address: int | slice) -> int | bytes:
        if isinstance(address, int):
            return self._read_bytes(address, 1)[0]
        if isinstance(address, slice):
            start = address.start or 0
            stop = address.stop
            if stop is None:
                raise ValueError("slice must have an explicit stop address")
            if address.step is not None and address.step != 1:
                raise ValueError("memory slices with step != 1 are not supported")
            length = stop - start
            if length <= 0:
                return b""
            return self._read_bytes(start, length)
        raise TypeError(f"indices must be int or slice, not {type(address).__name__}")

    def read(self, address: int, length: int) -> bytes:
        """Read a sequence of bytes from memory.

        Args:
            address: The starting address.
            length: Number of bytes to read.

        Returns:
            The bytes read from memory.
        """
        return self._read_bytes(address, length)

    def cast(self, fmt: str) -> TypedMemoryReader:
        """Return a typed view of memory using a struct format string.

        Args:
            fmt: A struct format string (e.g., "<H" for little-endian uint16).

        Returns:
            A TypedMemoryReader for typed access.
        """
        return TypedMemoryReader(self, fmt)


class MemoryWriter(MemoryAccessorBase):
    """Write memory operations."""

    @overload
    def __setitem__(self, address: int, value: int) -> None: ...

    @overload
    def __setitem__(self, address: slice, value: bytes) -> None: ...

    def __setitem__(self, address: int | slice, value: int | bytes) -> None:
        if isinstance(address, int):
            if not isinstance(value, int) or not (0 <= value <= 255):
                raise ValueError("single byte write requires int 0-255")
            self._write_bytes(address, bytes([value]))
        elif isinstance(address, slice):
            start = address.start or 0
            stop = address.stop
            if stop is None:
                raise ValueError("slice must have an explicit stop address")
            if address.step is not None and address.step != 1:
                raise ValueError("memory slices with step != 1 are not supported")
            slice_length = stop - start
            if not hasattr(value, "__len__"):
                raise TypeError("slice assignment requires bytes-like object")
            if len(value) != slice_length:
                raise ValueError(
                    f"slice assignment size mismatch: "
                    f"slice is {slice_length} bytes, data is {len(value)} bytes"
                )
            self._write_bytes(start, bytes(value))
        else:
            raise TypeError(f"indices must be int or slice, not {type(address).__name__}")

    def write(self, address: int, data: bytes) -> None:
        """Write a sequence of bytes to memory.

        Args:
            address: The starting address.
            data: The bytes to write.
        """
        self._write_bytes(address, bytes(data))


class PeekMemoryAccessor(MemoryReader):
    """Side-effect-free memory access (read-only).

    Reading I/O addresses through this accessor does not trigger hardware
    side effects. This is useful for inspecting memory-mapped I/O registers
    without affecting emulator state.
    """

    def _read_bytes(self, address: int, length: int) -> bytes:
        """Read bytes using PeekMemory RPC (no side effects)."""
        request = debugger_pb2.PeekMemoryRequest(address=address, length=length)
        response = self._stub.PeekMemory(request)
        return response.data


class BusMemoryAccessor(MemoryReader, MemoryWriter):
    """Side-effecting memory access (through memory bus).

    Reads and writes go through the memory bus exactly like the 6502 CPU
    would access memory. Reading or writing I/O addresses may trigger
    hardware side effects.
    """

    def _read_bytes(self, address: int, length: int) -> bytes:
        """Read bytes using ReadMemory RPC (may have side effects)."""
        request = debugger_pb2.ReadMemoryRequest(address=address, length=length)
        response = self._stub.ReadMemory(request)
        return response.data

    def _write_bytes(self, address: int, data: bytes) -> None:
        """Write bytes using WriteMemory RPC."""
        request = debugger_pb2.WriteMemoryRequest(address=address, data=data)
        response = self._stub.WriteMemory(request)
        if not response.success:
            raise MemoryAccessError(f"Failed to write {len(data)} bytes at ${address:04X}")

    def cast(self, fmt: str) -> TypedMemoryAccessor:
        """Return a typed view of memory using a struct format string.

        Args:
            fmt: A struct format string (e.g., "<H" for little-endian uint16).

        Returns:
            A TypedMemoryAccessor for typed read+write access.
        """
        return TypedMemoryAccessor(self, fmt)


class Memory:
    """Memory access namespace.

    Provides explicit access modes for reading and writing emulator memory:

    - `bus` - Side-effecting access (through memory bus like real hardware)
    - `peek` - Side-effect-free access (read-only)

    Usage:
        bbc.memory.bus[0x1000]           # Read with side effects
        bbc.memory.peek[0xFE4D]          # Read without side effects
        bbc.memory.bus[0x1000] = 0x42    # Write through bus
        bbc.memory.bus.cast("<H")[0x70]  # 16-bit little-endian read
    """

    def __init__(self, stub: debugger_pb2_grpc.DebuggerControlStub):
        """Create a memory interface.

        Args:
            stub: The gRPC stub for the DebuggerControl service.
        """
        self._stub = stub
        self._bus: BusMemoryAccessor | None = None
        self._peek: PeekMemoryAccessor | None = None

    @property
    def bus(self) -> BusMemoryAccessor:
        """Side-effecting memory access (through memory bus).

        Reads and writes go through the memory bus exactly like the 6502 CPU
        would access memory. Reading or writing I/O addresses may trigger
        hardware side effects.
        """
        if self._bus is None:
            self._bus = BusMemoryAccessor(self._stub)
        return self._bus

    @property
    def peek(self) -> PeekMemoryAccessor:
        """Side-effect-free memory access (read-only).

        Reading I/O addresses does not trigger hardware side effects.
        Write operations are not available on this accessor.
        """
        if self._peek is None:
            self._peek = PeekMemoryAccessor(self._stub)
        return self._peek

    def load(
        self, address: int, filepath: str | Path, max_length: int = 0x10000
    ) -> int:
        """Load a binary file into memory starting at address.

        Uses bus access (side-effecting writes).

        Args:
            address: The starting address to load at.
            filepath: Path to the binary file.
            max_length: Maximum bytes to load (default 64KB).

        Returns:
            The number of bytes written.

        Raises:
            FileNotFoundError: If the file doesn't exist.
        """
        data = Path(filepath).read_bytes()
        if len(data) > max_length:
            data = data[:max_length]
        available = 0x10000 - address
        if len(data) > available:
            data = data[:available]
        self.bus.write(address, data)
        return len(data)

    def save(self, address: int, length: int, filepath: str | Path) -> None:
        """Save a memory range to a binary file.

        Uses bus access (side-effecting reads).

        Args:
            address: The starting address to save from.
            length: Number of bytes to save.
            filepath: Path to save the binary file.
        """
        data = self.bus.read(address, length)
        Path(filepath).write_bytes(data)

    def fill(self, start: int, end: int, value: int = 0) -> None:
        """Fill a memory range with a value.

        Uses bus access (side-effecting writes).

        Args:
            start: Start address (inclusive).
            end: End address (exclusive).
            value: The byte value to fill with (default 0).
        """
        length = end - start
        if length <= 0:
            return
        self.bus.write(start, bytes([value] * length))

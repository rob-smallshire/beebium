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

"""Unit tests for the memory access module."""

from unittest.mock import MagicMock, patch

import pytest

from beebium.memory import (
    BusMemoryAccessor,
    Memory,
    MemoryReader,
    MemoryWriter,
    PeekMemoryAccessor,
    TypedMemoryAccessor,
    TypedMemoryReader,
)


class MockResponse:
    """Mock gRPC response."""

    def __init__(self, data: bytes = b"", success: bool = True):
        self.data = data
        self.success = success


@pytest.fixture
def mock_stub():
    """Create a mock gRPC stub."""
    stub = MagicMock()
    stub.ReadMemory.return_value = MockResponse(data=b"\x42")
    stub.PeekMemory.return_value = MockResponse(data=b"\x42")
    stub.WriteMemory.return_value = MockResponse(success=True)
    return stub


@pytest.fixture
def memory(mock_stub):
    """Create a Memory instance with mock stub."""
    return Memory(mock_stub)


class TestMemoryFacade:
    """Tests for the Memory facade class."""

    def test_bus_property_returns_bus_accessor(self, memory):
        """bus property returns a BusMemoryAccessor."""
        assert isinstance(memory.bus, BusMemoryAccessor)

    def test_peek_property_returns_peek_accessor(self, memory):
        """peek property returns a PeekMemoryAccessor."""
        assert isinstance(memory.peek, PeekMemoryAccessor)

    def test_bus_property_cached(self, memory):
        """bus property returns same instance on repeated access."""
        bus1 = memory.bus
        bus2 = memory.bus
        assert bus1 is bus2

    def test_peek_property_cached(self, memory):
        """peek property returns same instance on repeated access."""
        peek1 = memory.peek
        peek2 = memory.peek
        assert peek1 is peek2


class TestBusMemoryAccessor:
    """Tests for BusMemoryAccessor read/write operations."""

    def test_single_byte_read(self, memory, mock_stub):
        """Reading single address returns an int."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x42")
        result = memory.bus[0x1000]
        assert result == 0x42
        assert isinstance(result, int)

    def test_slice_read(self, memory, mock_stub):
        """Reading a slice returns bytes."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04")
        result = memory.bus[0x1000:0x1004]
        assert result == b"\x01\x02\x03\x04"
        assert isinstance(result, bytes)

    def test_single_byte_write(self, memory, mock_stub):
        """Writing single byte works."""
        memory.bus[0x1000] = 0x42
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\x42"

    def test_slice_write(self, memory, mock_stub):
        """Writing bytes to slice works."""
        memory.bus[0x1000:0x1004] = b"\x01\x02\x03\x04"
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\x01\x02\x03\x04"

    def test_read_method(self, memory, mock_stub):
        """read() method returns bytes."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04")
        result = memory.bus.read(0x1000, 4)
        assert result == b"\x01\x02\x03\x04"

    def test_write_method(self, memory, mock_stub):
        """write() method writes bytes."""
        memory.bus.write(0x1000, b"\x01\x02\x03\x04")
        mock_stub.WriteMemory.assert_called_once()


class TestPeekMemoryAccessor:
    """Tests for PeekMemoryAccessor read-only operations."""

    def test_single_byte_read(self, memory, mock_stub):
        """Reading single address returns an int."""
        mock_stub.PeekMemory.return_value = MockResponse(data=b"\x42")
        result = memory.peek[0x1000]
        assert result == 0x42

    def test_slice_read(self, memory, mock_stub):
        """Reading a slice returns bytes."""
        mock_stub.PeekMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04")
        result = memory.peek[0x1000:0x1004]
        assert result == b"\x01\x02\x03\x04"

    def test_peek_has_no_setitem(self, memory):
        """peek accessor has no __setitem__ method."""
        assert not hasattr(memory.peek, "__setitem__") or not callable(
            getattr(type(memory.peek), "__setitem__", None)
        )

    def test_peek_has_no_write_method(self, memory):
        """peek accessor has no write method."""
        assert not hasattr(memory.peek, "write")


class TestSliceLengthValidation:
    """Tests for slice assignment length validation."""

    def test_slice_assignment_length_mismatch_raises_valueerror(self, memory):
        """Slice assignment with wrong length raises ValueError."""
        with pytest.raises(ValueError, match="size mismatch"):
            memory.bus[0x1000:0x1004] = b"abc"  # 3 bytes, not 4

    def test_slice_assignment_exact_length_succeeds(self, memory, mock_stub):
        """Slice assignment with exact length succeeds."""
        memory.bus[0x1000:0x1004] = b"abcd"  # 4 bytes == 4 bytes
        mock_stub.WriteMemory.assert_called_once()

    def test_slice_assignment_empty_data_to_empty_slice(self, memory, mock_stub):
        """Empty data to empty slice succeeds."""
        memory.bus[0x1000:0x1000] = b""
        # Empty slice, no write should happen (length <= 0 check)
        # Actually let's check - the write should be called with empty data
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].data == b""

    def test_slice_assignment_too_long_raises_valueerror(self, memory):
        """Slice assignment with too much data raises ValueError."""
        with pytest.raises(ValueError, match="size mismatch"):
            memory.bus[0x1000:0x1002] = b"abcd"  # 4 bytes, not 2


class TestInvalidIndexType:
    """Tests for TypeError on invalid index types."""

    def test_bus_getitem_invalid_type_raises_typeerror(self, memory):
        """bus[string] raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.bus["invalid"]

    def test_bus_setitem_invalid_type_raises_typeerror(self, memory):
        """bus[string] = value raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            memory.bus["invalid"] = 0x42

    def test_peek_getitem_invalid_type_raises_typeerror(self, memory):
        """peek[string] raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.peek["invalid"]

    def test_bus_getitem_none_raises_typeerror(self, memory):
        """bus[None] raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.bus[None]


class TestSingleByteWriteValidation:
    """Tests for single byte write value validation."""

    def test_single_byte_write_negative_raises_valueerror(self, memory):
        """Writing negative value raises ValueError."""
        with pytest.raises(ValueError, match="single byte write requires int 0-255"):
            memory.bus[0x1000] = -1

    def test_single_byte_write_too_large_raises_valueerror(self, memory):
        """Writing value > 255 raises ValueError."""
        with pytest.raises(ValueError, match="single byte write requires int 0-255"):
            memory.bus[0x1000] = 256

    def test_single_byte_write_non_int_raises_valueerror(self, memory):
        """Writing non-int to single address raises ValueError."""
        with pytest.raises(ValueError, match="single byte write requires int 0-255"):
            memory.bus[0x1000] = "A"


class TestTypedMemoryAccessor:
    """Tests for cast() and typed memory access."""

    def test_cast_returns_typed_accessor(self, memory):
        """cast() returns TypedMemoryAccessor for bus."""
        typed = memory.bus.cast("<H")
        assert isinstance(typed, TypedMemoryAccessor)

    def test_cast_returns_typed_reader_for_peek(self, memory):
        """cast() returns TypedMemoryReader for peek."""
        typed = memory.peek.cast("<H")
        assert isinstance(typed, TypedMemoryReader)
        # And specifically NOT TypedMemoryAccessor
        assert type(typed) is TypedMemoryReader

    def test_cast_u16_read(self, memory, mock_stub):
        """Reading 16-bit value through cast."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x34\x12")
        result = memory.bus.cast("<H")[0x0070]
        assert result == 0x1234

    def test_cast_u16_write(self, memory, mock_stub):
        """Writing 16-bit value through cast."""
        memory.bus.cast("<H")[0x0070] = 0x1234
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x0070
        assert call_args[0][0].data == b"\x34\x12"

    def test_cast_u32_read(self, memory, mock_stub):
        """Reading 32-bit value through cast."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x78\x56\x34\x12")
        result = memory.bus.cast("<I")[0x1000]
        assert result == 0x12345678

    def test_cast_slice_read(self, memory, mock_stub):
        """Reading multiple values through cast slice."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x00\x02\x00\x03\x00")
        result = memory.bus.cast("<H")[0x1000:0x1006]
        assert result == (1, 2, 3)

    def test_cast_slice_misaligned_raises_valueerror(self, memory, mock_stub):
        """Misaligned cast slice raises ValueError."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04\x05")
        with pytest.raises(ValueError, match="not a multiple"):
            _ = memory.bus.cast("<H")[0x0070:0x0075]  # 5 bytes, not multiple of 2

    def test_cast_slice_write(self, memory, mock_stub):
        """Writing multiple values through cast slice."""
        memory.bus.cast("<H")[0x1000:0x1006] = (1, 2, 3)
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\x01\x00\x02\x00\x03\x00"

    def test_cast_invalid_index_type_raises_typeerror(self, memory):
        """cast accessor raises TypeError for invalid index."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.bus.cast("<H")["invalid"]

    def test_cast_setitem_invalid_index_type_raises_typeerror(self, memory):
        """cast accessor setitem raises TypeError for invalid index."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            memory.bus.cast("<H")["invalid"] = 0x1234


class TestConvenienceMethods:
    """Tests for Memory convenience methods (load, save, fill)."""

    def test_fill(self, memory, mock_stub):
        """fill() writes repeated bytes."""
        memory.fill(0x1000, 0x1004, 0xFF)
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\xFF\xFF\xFF\xFF"

    def test_fill_zero_length(self, memory, mock_stub):
        """fill() with zero length does nothing."""
        memory.fill(0x1000, 0x1000, 0xFF)
        mock_stub.WriteMemory.assert_not_called()

    def test_fill_negative_length(self, memory, mock_stub):
        """fill() with negative length does nothing."""
        memory.fill(0x1004, 0x1000, 0xFF)
        mock_stub.WriteMemory.assert_not_called()


class TestSliceEdgeCases:
    """Tests for slice edge cases."""

    def test_slice_without_stop_raises_valueerror(self, memory):
        """Slice without stop raises ValueError."""
        with pytest.raises(ValueError, match="explicit stop"):
            _ = memory.bus[0x1000:]

    def test_slice_with_step_raises_valueerror(self, memory):
        """Slice with step != 1 raises ValueError."""
        with pytest.raises(ValueError, match="step != 1"):
            _ = memory.bus[0x1000:0x1010:2]

    def test_empty_slice_returns_empty_bytes(self, memory, mock_stub):
        """Empty slice returns empty bytes."""
        result = memory.bus[0x1000:0x1000]
        assert result == b""

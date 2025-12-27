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
    AddressSpace,
    BusMemoryAccessor,
    Memory,
    MemoryReader,
    MemoryRegionInfo,
    MemoryWriter,
    PeekMemoryAccessor,
    Region,
    RegionBusAccessor,
    RegionPeekAccessor,
    TypedMemoryAccessor,
    TypedMemoryReader,
)


class MockResponse:
    """Mock gRPC response."""

    def __init__(self, data: bytes = b"", success: bool = True, error: str = ""):
        self.data = data
        self.success = success
        self.error = error


class MockRegionInfo:
    """Mock MemoryRegionInfo from proto."""

    def __init__(
        self,
        name: str = "main_ram",
        base_address: int = 0,
        size: int = 32768,
        readable: bool = True,
        writable: bool = True,
        has_side_effects: bool = False,
        populated: bool = True,
        active: bool = False,
    ):
        self.name = name
        self.base_address = base_address
        self.size = size
        self.readable = readable
        self.writable = writable
        self.has_side_effects = has_side_effects
        self.populated = populated
        self.active = active


class MockGetMemoryRegionsResponse:
    """Mock GetMemoryRegions response."""

    def __init__(self):
        self.machine_type = "ModelB"
        self.regions = [
            MockRegionInfo("main_ram", 0x0000, 32768),
            MockRegionInfo("mos_rom", 0xC000, 16384, writable=False),
            MockRegionInfo("bank_0", 0x8000, 16384, active=True),
        ]


@pytest.fixture
def mock_stub():
    """Create a mock gRPC stub."""
    stub = MagicMock()
    stub.ReadMemory.return_value = MockResponse(data=b"\x42")
    stub.PeekMemory.return_value = MockResponse(data=b"\x42")
    stub.WriteMemory.return_value = MockResponse(success=True)
    stub.ReadRegion.return_value = MockResponse(data=b"\x42")
    stub.PeekRegion.return_value = MockResponse(data=b"\x42")
    stub.WriteRegion.return_value = MockResponse(success=True)
    stub.GetMemoryRegions.return_value = MockGetMemoryRegionsResponse()
    return stub


@pytest.fixture
def memory(mock_stub):
    """Create a Memory instance with mock stub."""
    return Memory(mock_stub)


class TestMemoryFacade:
    """Tests for the Memory facade class."""

    def test_address_property_returns_address_space(self, memory):
        """address property returns an AddressSpace."""
        assert isinstance(memory.address, AddressSpace)

    def test_address_bus_returns_bus_accessor(self, memory):
        """address.bus returns a BusMemoryAccessor."""
        assert isinstance(memory.address.bus, BusMemoryAccessor)

    def test_address_peek_returns_peek_accessor(self, memory):
        """address.peek returns a PeekMemoryAccessor."""
        assert isinstance(memory.address.peek, PeekMemoryAccessor)

    def test_address_property_cached(self, memory):
        """address property returns same instance on repeated access."""
        addr1 = memory.address
        addr2 = memory.address
        assert addr1 is addr2

    def test_region_returns_region(self, memory):
        """region() returns a Region."""
        assert isinstance(memory.region("main_ram"), Region)

    def test_regions_returns_list(self, memory):
        """regions property returns a list of MemoryRegionInfo."""
        regions = memory.regions
        assert isinstance(regions, list)
        assert len(regions) == 3
        assert all(isinstance(r, MemoryRegionInfo) for r in regions)

    def test_regions_cached(self, memory, mock_stub):
        """regions property is cached."""
        _ = memory.regions
        _ = memory.regions
        mock_stub.GetMemoryRegions.assert_called_once()

    def test_machine_type_returns_string(self, memory):
        """machine_type property returns a string."""
        assert memory.machine_type == "ModelB"


class TestBusMemoryAccessor:
    """Tests for BusMemoryAccessor read/write operations."""

    def test_single_byte_read(self, memory, mock_stub):
        """Reading single address returns an int."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x42")
        result = memory.address.bus[0x1000]
        assert result == 0x42
        assert isinstance(result, int)

    def test_slice_read(self, memory, mock_stub):
        """Reading a slice returns bytes."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04")
        result = memory.address.bus[0x1000:0x1004]
        assert result == b"\x01\x02\x03\x04"
        assert isinstance(result, bytes)

    def test_single_byte_write(self, memory, mock_stub):
        """Writing single byte works."""
        memory.address.bus[0x1000] = 0x42
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\x42"

    def test_slice_write(self, memory, mock_stub):
        """Writing bytes to slice works."""
        memory.address.bus[0x1000:0x1004] = b"\x01\x02\x03\x04"
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\x01\x02\x03\x04"

    def test_read_method(self, memory, mock_stub):
        """read() method returns bytes."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04")
        result = memory.address.bus.read(0x1000, 4)
        assert result == b"\x01\x02\x03\x04"

    def test_write_method(self, memory, mock_stub):
        """write() method writes bytes."""
        memory.address.bus.write(0x1000, b"\x01\x02\x03\x04")
        mock_stub.WriteMemory.assert_called_once()


class TestPeekMemoryAccessor:
    """Tests for PeekMemoryAccessor read-only operations."""

    def test_single_byte_read(self, memory, mock_stub):
        """Reading single address returns an int."""
        mock_stub.PeekMemory.return_value = MockResponse(data=b"\x42")
        result = memory.address.peek[0x1000]
        assert result == 0x42

    def test_slice_read(self, memory, mock_stub):
        """Reading a slice returns bytes."""
        mock_stub.PeekMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04")
        result = memory.address.peek[0x1000:0x1004]
        assert result == b"\x01\x02\x03\x04"

    def test_peek_has_no_setitem(self, memory):
        """peek accessor has no __setitem__ method."""
        assert not hasattr(memory.address.peek, "__setitem__") or not callable(
            getattr(type(memory.address.peek), "__setitem__", None)
        )

    def test_peek_has_no_write_method(self, memory):
        """peek accessor has no write method."""
        assert not hasattr(memory.address.peek, "write")


class TestSliceLengthValidation:
    """Tests for slice assignment length validation."""

    def test_slice_assignment_length_mismatch_raises_valueerror(self, memory):
        """Slice assignment with wrong length raises ValueError."""
        with pytest.raises(ValueError, match="size mismatch"):
            memory.address.bus[0x1000:0x1004] = b"abc"  # 3 bytes, not 4

    def test_slice_assignment_exact_length_succeeds(self, memory, mock_stub):
        """Slice assignment with exact length succeeds."""
        memory.address.bus[0x1000:0x1004] = b"abcd"  # 4 bytes == 4 bytes
        mock_stub.WriteMemory.assert_called_once()

    def test_slice_assignment_empty_data_to_empty_slice(self, memory, mock_stub):
        """Empty data to empty slice succeeds."""
        memory.address.bus[0x1000:0x1000] = b""
        # Empty slice, no write should happen (length <= 0 check)
        # Actually let's check - the write should be called with empty data
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].data == b""

    def test_slice_assignment_too_long_raises_valueerror(self, memory):
        """Slice assignment with too much data raises ValueError."""
        with pytest.raises(ValueError, match="size mismatch"):
            memory.address.bus[0x1000:0x1002] = b"abcd"  # 4 bytes, not 2


class TestInvalidIndexType:
    """Tests for TypeError on invalid index types."""

    def test_bus_getitem_invalid_type_raises_typeerror(self, memory):
        """bus[string] raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.address.bus["invalid"]

    def test_bus_setitem_invalid_type_raises_typeerror(self, memory):
        """bus[string] = value raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            memory.address.bus["invalid"] = 0x42

    def test_peek_getitem_invalid_type_raises_typeerror(self, memory):
        """peek[string] raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.address.peek["invalid"]

    def test_bus_getitem_none_raises_typeerror(self, memory):
        """bus[None] raises TypeError."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.address.bus[None]


class TestSingleByteWriteValidation:
    """Tests for single byte write value validation."""

    def test_single_byte_write_negative_raises_valueerror(self, memory):
        """Writing negative value raises ValueError."""
        with pytest.raises(ValueError, match="single byte write requires int 0-255"):
            memory.address.bus[0x1000] = -1

    def test_single_byte_write_too_large_raises_valueerror(self, memory):
        """Writing value > 255 raises ValueError."""
        with pytest.raises(ValueError, match="single byte write requires int 0-255"):
            memory.address.bus[0x1000] = 256

    def test_single_byte_write_non_int_raises_valueerror(self, memory):
        """Writing non-int to single address raises ValueError."""
        with pytest.raises(ValueError, match="single byte write requires int 0-255"):
            memory.address.bus[0x1000] = "A"


class TestTypedMemoryAccessor:
    """Tests for cast() and typed memory access."""

    def test_cast_returns_typed_accessor(self, memory):
        """cast() returns TypedMemoryAccessor for bus."""
        typed = memory.address.bus.cast("<H")
        assert isinstance(typed, TypedMemoryAccessor)

    def test_cast_returns_typed_reader_for_peek(self, memory):
        """cast() returns TypedMemoryReader for peek."""
        typed = memory.address.peek.cast("<H")
        assert isinstance(typed, TypedMemoryReader)
        # And specifically NOT TypedMemoryAccessor
        assert type(typed) is TypedMemoryReader

    def test_cast_u16_read(self, memory, mock_stub):
        """Reading 16-bit value through cast."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x34\x12")
        result = memory.address.bus.cast("<H")[0x0070]
        assert result == 0x1234

    def test_cast_u16_write(self, memory, mock_stub):
        """Writing 16-bit value through cast."""
        memory.address.bus.cast("<H")[0x0070] = 0x1234
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x0070
        assert call_args[0][0].data == b"\x34\x12"

    def test_cast_u32_read(self, memory, mock_stub):
        """Reading 32-bit value through cast."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x78\x56\x34\x12")
        result = memory.address.bus.cast("<I")[0x1000]
        assert result == 0x12345678

    def test_cast_slice_read(self, memory, mock_stub):
        """Reading multiple values through cast slice."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x00\x02\x00\x03\x00")
        result = memory.address.bus.cast("<H")[0x1000:0x1006]
        assert result == (1, 2, 3)

    def test_cast_slice_misaligned_raises_valueerror(self, memory, mock_stub):
        """Misaligned cast slice raises ValueError."""
        mock_stub.ReadMemory.return_value = MockResponse(data=b"\x01\x02\x03\x04\x05")
        with pytest.raises(ValueError, match="not a multiple"):
            _ = memory.address.bus.cast("<H")[0x0070:0x0075]  # 5 bytes, not multiple of 2

    def test_cast_slice_write(self, memory, mock_stub):
        """Writing multiple values through cast slice."""
        memory.address.bus.cast("<H")[0x1000:0x1006] = (1, 2, 3)
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\x01\x00\x02\x00\x03\x00"

    def test_cast_invalid_index_type_raises_typeerror(self, memory):
        """cast accessor raises TypeError for invalid index."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            _ = memory.address.bus.cast("<H")["invalid"]

    def test_cast_setitem_invalid_index_type_raises_typeerror(self, memory):
        """cast accessor setitem raises TypeError for invalid index."""
        with pytest.raises(TypeError, match="indices must be int or slice"):
            memory.address.bus.cast("<H")["invalid"] = 0x1234


class TestAccessorUtilityMethods:
    """Tests for utility methods on accessors (load, save, fill)."""

    def test_fill(self, memory, mock_stub):
        """fill() writes repeated bytes."""
        memory.address.bus.fill(0x1000, 0x1004, 0xFF)
        mock_stub.WriteMemory.assert_called_once()
        call_args = mock_stub.WriteMemory.call_args
        assert call_args[0][0].address == 0x1000
        assert call_args[0][0].data == b"\xFF\xFF\xFF\xFF"

    def test_fill_zero_length(self, memory, mock_stub):
        """fill() with zero length does nothing."""
        memory.address.bus.fill(0x1000, 0x1000, 0xFF)
        mock_stub.WriteMemory.assert_not_called()

    def test_fill_negative_length(self, memory, mock_stub):
        """fill() with negative length does nothing."""
        memory.address.bus.fill(0x1004, 0x1000, 0xFF)
        mock_stub.WriteMemory.assert_not_called()


class TestSliceEdgeCases:
    """Tests for slice edge cases."""

    def test_slice_without_stop_raises_valueerror(self, memory):
        """Slice without stop raises ValueError."""
        with pytest.raises(ValueError, match="explicit stop"):
            _ = memory.address.bus[0x1000:]

    def test_slice_with_step_raises_valueerror(self, memory):
        """Slice with step != 1 raises ValueError."""
        with pytest.raises(ValueError, match="step != 1"):
            _ = memory.address.bus[0x1000:0x1010:2]

    def test_empty_slice_returns_empty_bytes(self, memory, mock_stub):
        """Empty slice returns empty bytes."""
        result = memory.address.bus[0x1000:0x1000]
        assert result == b""


class TestRegionAccess:
    """Tests for region-based memory access."""

    def test_region_returns_region_object(self, memory):
        """region() returns a Region object."""
        region = memory.region("main_ram")
        assert isinstance(region, Region)
        assert region.name == "main_ram"

    def test_region_bus_returns_region_bus_accessor(self, memory):
        """region().bus returns a RegionBusAccessor."""
        region = memory.region("main_ram")
        assert isinstance(region.bus, RegionBusAccessor)

    def test_region_peek_returns_region_peek_accessor(self, memory):
        """region().peek returns a RegionPeekAccessor."""
        region = memory.region("main_ram")
        assert isinstance(region.peek, RegionPeekAccessor)

    def test_region_bus_read(self, memory, mock_stub):
        """Reading from region.bus works."""
        mock_stub.ReadRegion.return_value = MockResponse(data=b"\x42")
        result = memory.region("main_ram").bus[0x1234]
        assert result == 0x42
        mock_stub.ReadRegion.assert_called_once()
        call_args = mock_stub.ReadRegion.call_args
        assert call_args[0][0].region_name == "main_ram"
        assert call_args[0][0].address == 0x1234

    def test_region_peek_read(self, memory, mock_stub):
        """Reading from region.peek works."""
        mock_stub.PeekRegion.return_value = MockResponse(data=b"\x42")
        result = memory.region("main_ram").peek[0x1234]
        assert result == 0x42
        mock_stub.PeekRegion.assert_called_once()
        call_args = mock_stub.PeekRegion.call_args
        assert call_args[0][0].region_name == "main_ram"
        assert call_args[0][0].address == 0x1234

    def test_region_bus_write(self, memory, mock_stub):
        """Writing to region.bus works."""
        memory.region("main_ram").bus[0x1234] = 0x42
        mock_stub.WriteRegion.assert_called_once()
        call_args = mock_stub.WriteRegion.call_args
        assert call_args[0][0].region_name == "main_ram"
        assert call_args[0][0].address == 0x1234
        assert call_args[0][0].data == b"\x42"

    def test_region_bus_cast(self, memory, mock_stub):
        """Typed access via region.bus.cast() works."""
        mock_stub.ReadRegion.return_value = MockResponse(data=b"\x34\x12")
        result = memory.region("main_ram").bus.cast("<H")[0x1234]
        assert result == 0x1234

    def test_region_info_from_cache(self, memory, mock_stub):
        """Region gets info from cache after regions are fetched."""
        # Trigger region cache fetch
        _ = memory.regions
        # Now get a region - it should have the cached info
        region = memory.region("main_ram")
        assert region.base_address == 0x0000
        assert region.size == 32768


class TestMemoryRegionInfo:
    """Tests for MemoryRegionInfo dataclass."""

    def test_memory_region_info_fields(self, memory):
        """MemoryRegionInfo has expected fields."""
        regions = memory.regions
        main_ram = next(r for r in regions if r.name == "main_ram")
        assert main_ram.name == "main_ram"
        assert main_ram.base_address == 0x0000
        assert main_ram.size == 32768
        assert main_ram.readable is True
        assert main_ram.writable is True
        assert main_ram.has_side_effects is False
        assert main_ram.populated is True

    def test_memory_region_info_is_frozen(self, memory):
        """MemoryRegionInfo is immutable."""
        regions = memory.regions
        with pytest.raises(AttributeError):
            regions[0].name = "modified"

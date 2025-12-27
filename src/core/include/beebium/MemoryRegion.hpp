// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace beebium {

// Flags describing memory region capabilities.
// These are used by debugger clients to discover what operations are available.
enum class RegionFlags : uint8_t {
    None           = 0,
    Readable       = 1 << 0,  // Region can be read (peek/read)
    Writable       = 1 << 1,  // Region can be written
    HasSideEffects = 1 << 2,  // read() may differ from peek() (e.g., clears registers)
    Populated      = 1 << 3,  // Region has content (for optional banks)
    Active         = 1 << 4,  // Currently selected/mapped (for banks)
};

inline RegionFlags operator|(RegionFlags a, RegionFlags b) {
    return static_cast<RegionFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline RegionFlags operator&(RegionFlags a, RegionFlags b) {
    return static_cast<RegionFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool has_flag(RegionFlags flags, RegionFlags flag) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// Information about a memory region exposed by a hardware configuration.
// Used for debugger discovery of available memory areas.
// Named "Descriptor" to avoid collision with protobuf-generated MemoryRegionInfo.
struct MemoryRegionDescriptor {
    std::string_view name;  // Region identifier (e.g., "main_ram", "bank_0")
    uint32_t base_address;  // Base address in CPU address space
    uint32_t size;          // Size in bytes
    RegionFlags flags;      // Capability flags
};

} // namespace beebium

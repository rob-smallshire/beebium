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

namespace beebium {

// Empty ROM slot - returns 0xFF for all reads, ignores writes.
// Value type satisfying MemoryMappedDevice concept (no inheritance).
struct EmptySlot {
    static constexpr size_t size = 16384;  // 16KB, matches sideways ROM size

    uint8_t read(uint16_t) const { return 0xFF; }
    void write(uint16_t, uint8_t) {}

    // Introspection
    static constexpr bool is_empty() { return true; }
    static constexpr bool is_writable() { return false; }
};

// Global instance for unpopulated banks in BankedMemory
inline EmptySlot default_empty_slot;

} // namespace beebium

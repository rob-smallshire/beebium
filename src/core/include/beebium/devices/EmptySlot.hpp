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

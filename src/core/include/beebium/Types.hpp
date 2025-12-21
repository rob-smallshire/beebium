#ifndef BEEBIUM_TYPES_HPP
#define BEEBIUM_TYPES_HPP

#include <cstdint>
#include <cstddef>

namespace beebium {

// Page size for memory banking (4KB pages, 16 total = 64KB)
constexpr size_t kPageSizeBytes = 4096;
constexpr size_t kNumPages = 16;
constexpr size_t kAddressSpaceSize = 65536;

// ROM bank constants
constexpr size_t kRomBankSize = 16384;  // 16KB per ROM bank
constexpr size_t kNumRomBanks = 16;     // 16 sideways ROM banks

// BBC Model B memory regions
constexpr uint16_t kRamStart = 0x0000;
constexpr uint16_t kRamEnd = 0x7FFF;      // 32KB RAM

constexpr uint16_t kRomStart = 0x8000;
constexpr uint16_t kRomEnd = 0xBFFF;      // Paged ROM area (16KB)

constexpr uint16_t kMosStart = 0xC000;
constexpr uint16_t kMosEnd = 0xFFFF;      // MOS ROM (16KB)

// Hardware registers (memory-mapped I/O)
constexpr uint16_t kSheilaStart = 0xFE00;
constexpr uint16_t kSheilaEnd = 0xFEFF;

// ROMSEL register (selects paged ROM bank)
constexpr uint16_t kRomselAddr = 0xFE30;

// Strong type for page indices
struct PageIndex {
    uint8_t value;

    constexpr explicit PageIndex(uint8_t v) : value(v) {}

    static constexpr PageIndex from_address(uint16_t addr) {
        return PageIndex(static_cast<uint8_t>(addr >> 12));
    }
};

// Strong type for ROM bank indices
struct RomBankIndex {
    uint8_t value;

    constexpr explicit RomBankIndex(uint8_t v) : value(v & 0x0F) {}
};

} // namespace beebium

#endif // BEEBIUM_TYPES_HPP

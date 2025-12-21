#ifndef BEEBIUM_MODEL_B_MEMORY_HPP
#define BEEBIUM_MODEL_B_MEMORY_HPP

#include "Types.hpp"
#include <array>
#include <cstdint>
#include <functional>

namespace beebium {

// Page abstraction for fast memory access with bank switching support.
// Inspired by B2's BigPage pattern.
struct Page {
    const uint8_t* read = nullptr;   // Read pointer (nullptr if not readable)
    uint8_t* write = nullptr;        // Write pointer (nullptr if not writable)
    PageIndex index{0};              // Index of this page (0-15)
};

// Callback for memory-mapped I/O reads
using IoReadCallback = std::function<uint8_t(uint16_t addr)>;

// Callback for memory-mapped I/O writes
using IoWriteCallback = std::function<void(uint16_t addr, uint8_t value)>;

// BBC Model B memory policy.
// Implements the standard Model B memory map:
//   0x0000-0x7FFF: 32KB RAM
//   0x8000-0xBFFF: Paged ROM (16 banks, selected via ROMSEL)
//   0xC000-0xFFFF: MOS ROM
//   0xFE00-0xFEFF: SHEILA (memory-mapped I/O)
//
class ModelBMemory {
public:
    ModelBMemory();

    // Required policy interface
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);
    void reset();

    // Fast page-based access (for future optimization)
    const Page& page(PageIndex idx) const { return pages_[idx.value]; }

    // ROM loading
    void load_mos(const uint8_t* data, size_t size);
    void load_rom_bank(RomBankIndex bank, const uint8_t* data, size_t size);

    // ROM bank selection
    void set_rom_bank(RomBankIndex bank);
    RomBankIndex rom_bank() const { return current_rom_bank_; }

    // Direct RAM access (for testing/debugging)
    uint8_t* ram_ptr() { return ram_.data(); }
    const uint8_t* ram_ptr() const { return ram_.data(); }

    // I/O callbacks (for peripheral integration)
    void set_io_read_callback(IoReadCallback callback);
    void set_io_write_callback(IoWriteCallback callback);

private:
    void update_page_tables();
    bool is_io_address(uint16_t addr) const;

    // Main RAM (32KB for Model B)
    std::array<uint8_t, 32768> ram_{};

    // MOS ROM (16KB, always mapped at C000-FFFF)
    std::array<uint8_t, 16384> mos_{};

    // Sideways ROM banks (16 banks x 16KB each)
    std::array<std::array<uint8_t, kRomBankSize>, kNumRomBanks> rom_banks_{};

    // Currently selected ROM bank
    RomBankIndex current_rom_bank_{0};

    // Page table for fast access
    std::array<Page, kNumPages> pages_{};

    // I/O callbacks
    IoReadCallback io_read_callback_;
    IoWriteCallback io_write_callback_;

    // Buffer for writes to read-only memory (discarded)
    static inline std::array<uint8_t, kPageSizeBytes> discard_buffer_{};
};

} // namespace beebium

#endif // BEEBIUM_MODEL_B_MEMORY_HPP

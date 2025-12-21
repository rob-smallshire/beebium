#include "beebium/Memory.hpp"
#include <algorithm>
#include <cstring>

namespace beebium {

Memory::Memory() {
    reset();
}

void Memory::reset() {
    // Clear RAM
    std::fill(ram_.begin(), ram_.end(), 0);

    // Initialize ROMs to 0xFF (empty EPROM state)
    std::fill(mos_.begin(), mos_.end(), 0xFF);
    for (auto& bank : rom_banks_) {
        std::fill(bank.begin(), bank.end(), 0xFF);
    }

    // Default to ROM bank 0
    current_rom_bank_ = RomBankIndex{0};

    // Set up page tables
    update_page_tables();
}

void Memory::update_page_tables() {
    // Pages 0-7 (0x0000-0x7FFF): RAM
    for (uint8_t i = 0; i < 8; ++i) {
        pages_[i].index = PageIndex{i};
        pages_[i].read = ram_.data() + (i * kPageSizeBytes);
        pages_[i].write = ram_.data() + (i * kPageSizeBytes);
    }

    // Pages 8-11 (0x8000-0xBFFF): Paged ROM
    const auto& rom = rom_banks_[current_rom_bank_.value];
    for (uint8_t i = 8; i < 12; ++i) {
        pages_[i].index = PageIndex{i};
        pages_[i].read = rom.data() + ((i - 8) * kPageSizeBytes);
        pages_[i].write = discard_buffer_.data();  // Writes to ROM are discarded
    }

    // Pages 12-15 (0xC000-0xFFFF): MOS ROM
    // Note: Page 15 contains SHEILA (I/O) at FE00-FEFF, handled separately
    for (uint8_t i = 12; i < 16; ++i) {
        pages_[i].index = PageIndex{i};
        pages_[i].read = mos_.data() + ((i - 12) * kPageSizeBytes);
        pages_[i].write = discard_buffer_.data();  // Writes to ROM are discarded
    }
}

bool Memory::is_io_address(uint16_t addr) const {
    return addr >= kSheilaStart && addr <= kSheilaEnd;
}

uint8_t Memory::read(uint16_t addr) const {
    // Check for I/O addresses first
    if (is_io_address(addr)) {
        if (io_read_callback_) {
            return io_read_callback_(addr);
        }
        return 0xFF;  // Default: open bus
    }

    // Fast path using page tables
    const auto page_idx = PageIndex::from_address(addr);
    const auto& page = pages_[page_idx.value];
    const uint16_t offset = addr & 0x0FFF;  // Offset within page

    if (page.read) {
        return page.read[offset];
    }
    return 0xFF;  // Unmapped
}

void Memory::write(uint16_t addr, uint8_t value) {
    // Check for I/O addresses first
    if (is_io_address(addr)) {
        // Handle ROMSEL specially
        if (addr == kRomselAddr) {
            set_rom_bank(RomBankIndex{value});
        }
        if (io_write_callback_) {
            io_write_callback_(addr, value);
        }
        return;
    }

    // Fast path using page tables
    const auto page_idx = PageIndex::from_address(addr);
    auto& page = pages_[page_idx.value];
    const uint16_t offset = addr & 0x0FFF;

    if (page.write) {
        page.write[offset] = value;
    }
    // Writes to read-only memory or unmapped regions are silently discarded
}

void Memory::load_mos(const uint8_t* data, size_t size) {
    size_t copy_size = std::min(size, mos_.size());
    std::memcpy(mos_.data(), data, copy_size);
    update_page_tables();
}

void Memory::load_rom_bank(RomBankIndex bank, const uint8_t* data, size_t size) {
    size_t copy_size = std::min(size, rom_banks_[bank.value].size());
    std::memcpy(rom_banks_[bank.value].data(), data, copy_size);

    // Update page tables if this is the currently selected bank
    if (bank.value == current_rom_bank_.value) {
        update_page_tables();
    }
}

void Memory::set_rom_bank(RomBankIndex bank) {
    if (bank.value != current_rom_bank_.value) {
        current_rom_bank_ = bank;
        update_page_tables();
    }
}

void Memory::set_io_read_callback(IoReadCallback callback) {
    io_read_callback_ = std::move(callback);
}

void Memory::set_io_write_callback(IoWriteCallback callback) {
    io_write_callback_ = std::move(callback);
}

} // namespace beebium

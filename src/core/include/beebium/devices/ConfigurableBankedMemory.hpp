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

#include "ConfigurableSlot.hpp"
#include <array>
#include <cstdint>

namespace beebium {

// ConfigurableBankedMemory models a ROM/RAM expansion board with 16 independent
// slots that can be individually configured at runtime.
//
// Unlike the stock BBC Model B (which has 4 physical sockets with aliasing),
// a ROM/RAM board provides full 4-bit decoding so each of the 16 ROMSEL values
// addresses a unique slot. This is typical of third-party expansion boards
// like those from Watford Electronics or modern replacements.
//
// All slots start as Empty (returning 0xFF). Configure via configure_slot()
// or load_rom_to_slot(). For differential testing with jsbeeb, configure
// slots 0-7 as RAM via CLI or API.
//
class ConfigurableBankedMemory {
    std::array<ConfigurableSlot, 16> slots_;
    uint8_t selected_bank_ = 0;

public:
    static constexpr size_t num_banks = 16;
    static constexpr size_t bank_size = 16384;

    // Default constructor: all slots Empty (return 0xFF)
    ConfigurableBankedMemory() = default;

    // MemoryMappedDevice interface
    uint8_t read(uint16_t offset) const {
        return slots_[selected_bank_].read(offset);
    }

    void write(uint16_t offset, uint8_t value) {
        slots_[selected_bank_].write(offset, value);
    }

    // Bank selection (via ROMSEL)
    uint8_t selected_bank() const { return selected_bank_; }

    void select_bank(uint8_t bank) {
        selected_bank_ = bank & 0x0F;
    }

    // Direct slot access
    ConfigurableSlot& slot(uint8_t slot_idx) {
        return slots_[slot_idx % num_banks];
    }

    const ConfigurableSlot& slot(uint8_t slot_idx) const {
        return slots_[slot_idx % num_banks];
    }

    // Configure a slot's type
    void configure_slot(uint8_t slot_idx, SlotType type) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].set_type(type);
        }
    }

    // Load ROM data into a slot
    void load_rom(uint8_t slot_idx, const uint8_t* data, size_t len) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].load(data, len);
        }
    }

    // Load ROM and set type in one call
    void load_rom_to_slot(uint8_t slot_idx, const uint8_t* data, size_t len) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].set_type(SlotType::Rom);
            slots_[slot_idx].load(data, len);
        }
    }

    // Check if a bank is populated
    bool is_bank_populated(uint8_t bank) const {
        if (bank < num_banks) {
            return slots_[bank].is_populated();
        }
        return false;
    }

    // Get slot type
    SlotType bank_type(uint8_t bank) const {
        if (bank < num_banks) {
            return slots_[bank].type();
        }
        return SlotType::Empty;
    }

    // Direct bank access for debugger - side-effect free read
    uint8_t peek_bank(uint8_t bank, uint16_t offset) const {
        if (bank < num_banks) {
            return slots_[bank].read(offset);
        }
        return 0xFF;
    }

    // Direct bank access - normal read
    uint8_t read_bank(uint8_t bank, uint16_t offset) {
        if (bank < num_banks) {
            return slots_[bank].read(offset);
        }
        return 0xFF;
    }

    // Direct bank access - write
    void write_bank(uint8_t bank, uint16_t offset, uint8_t value) {
        if (bank < num_banks) {
            slots_[bank].write(offset, value);
        }
    }

    // Set image name for a slot
    void set_slot_image_name(uint8_t slot_idx, std::string_view name) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].set_image_name(name);
        }
    }

    // Get image name for a slot
    std::string_view slot_image_name(uint8_t slot_idx) const {
        if (slot_idx < num_banks) {
            return slots_[slot_idx].image_name();
        }
        return "";
    }

    // Clear a slot's contents
    void clear_slot(uint8_t slot_idx) {
        if (slot_idx < num_banks) {
            slots_[slot_idx].clear();
            slots_[slot_idx].clear_image_name();
        }
    }

    // Trait to indicate this memory type does not have aliasing
    static constexpr bool has_aliasing = false;

    // =========================================================================
    // Unified ROM Loading API
    // =========================================================================

    // Load ROM data into a slot, automatically configuring as ROM type.
    // This is the primary method for loading ROMs - it encapsulates the
    // configure + load pattern to avoid the bug where load is called without
    // first setting the slot type.
    //
    // image_name, when non-empty, is stored on the destination slot so that
    // gRPC clients (e.g. the macOS Memory sidebar) can render and act on it.
    void load_sideways_rom(uint8_t slot, const uint8_t* data, size_t len,
                          std::string_view image_name = "") {
        configure_slot(slot, SlotType::Rom);
        load_rom(slot, data, len);
        if (!image_name.empty() && slot < num_banks) {
            slots_[slot].set_image_name(image_name);
        }
    }

    // Load data into a slot WITHOUT changing slot type.
    // Used for pre-initializing RAM with saved state (e.g., battery-backed RAM).
    void load_sideways_data(uint8_t slot, const uint8_t* data, size_t len,
                           std::string_view image_name = "") {
        if (slot < num_banks) {
            slots_[slot].load(data, len);
            if (!image_name.empty()) {
                slots_[slot].set_image_name(image_name);
            }
        }
    }

    // Check if a slot can have ROM loaded (all slots are loadable)
    static constexpr bool is_slot_loadable(uint8_t slot) { return slot < num_banks; }

    // Configure a slot as writable RAM
    void configure_slot_as_ram(uint8_t slot) {
        configure_slot(slot, SlotType::Ram);
    }

    // Configure a slot as empty
    void configure_slot_as_empty(uint8_t slot) {
        configure_slot(slot, SlotType::Empty);
    }

    // Per-bank write-protect control (models a board's write-protect switch).
    void set_slot_write_protected(uint8_t slot, bool protect) {
        if (slot < num_banks) {
            slots_[slot].set_write_protected(protect);
        }
    }

    bool is_slot_write_protected(uint8_t slot) const {
        if (slot < num_banks) {
            return slots_[slot].is_write_protected();
        }
        return false;
    }

    // Uniform per-slot query (see SlotInfo in ConfigurableSlot.hpp).
    SlotInfo slot_info(uint8_t slot) const {
        if (slot >= num_banks) return {};
        const auto& s = slots_[slot];
        return {s.type(), s.is_populated(), std::string(s.image_name()),
                s.is_write_protected()};
    }
};

} // namespace beebium

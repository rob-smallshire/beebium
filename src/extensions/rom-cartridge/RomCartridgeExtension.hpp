// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXT_ROM_CARTRIDGE_EXTENSION_HPP
#define BEEBIUM_EXT_ROM_CARTRIDGE_EXTENSION_HPP

#include <beebium/devices/SidewaysBankProvider.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

class CartridgeSlot;

// Built-in PeripheralExtension, and the simplest cartridge: a ROM cartridge
// in one of the Master 128's cartridge slots, the one its placement names
// (cartridge-rear, banks 0 and 1, or cartridge-front, banks 2 and 3).
//
// A cartridge provides its slot's sideways banks (CartridgeSlot.hpp): this
// one is a SidewaysBankProvider holding its ROM images, fitted to the slot
// in init(). `low` is the ROM on the slot's even bank (ROMQA 0) and `high`,
// if given, the one on its odd bank; a 16K cartridge gives `low` alone,
// which answers on both. Writes go nowhere. It decodes nothing on the
// cartridge bus's FRED and JIM. A RAM cartridge, or one with I/O, would
// follow the same shape with a writable provider or bus handlers beside.
//
// CLI: --rom-cartridge low=<rom>[:high=<rom>][:at=cartridge-rear|cartridge-front]
class RomCartridgeExtension : public PeripheralExtension, public SidewaysBankProvider {
public:
    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override { return {}; }
    std::span<const std::string_view> provides() const override { return {}; }
    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // SidewaysBankProvider: bank 0 the even bank, bank 1 the odd one.
    uint8_t read_bank(uint8_t bank, uint16_t offset) override { return byte(bank, offset); }
    uint8_t peek_bank(uint8_t bank, uint16_t offset) const override { return byte(bank, offset); }
    void write_bank(uint8_t, uint16_t, uint8_t) override {}
    SlotInfo bank_info(uint8_t bank) const override;

private:
    // The image on `bank`: high on bank 1 when there is one, else low.
    const std::vector<uint8_t>& image(uint8_t bank) const {
        return (bank & 1) && !images_[1].empty() ? images_[1] : images_[0];
    }
    const std::string& image_name(uint8_t bank) const {
        return (bank & 1) && !images_[1].empty() ? names_[1] : names_[0];
    }
    // A ROM smaller than 16K repeats through the bank, as its address lines
    // leave the top ones undecoded.
    uint8_t byte(uint8_t bank, uint16_t offset) const {
        const auto& rom = image(bank);
        return rom.empty() ? 0xFF : rom[offset % rom.size()];
    }

    std::array<std::vector<uint8_t>, 2> images_;
    std::array<std::string, 2> names_;
    CartridgeSlot* slot_ = nullptr;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_ROM_CARTRIDGE_EXTENSION_HPP

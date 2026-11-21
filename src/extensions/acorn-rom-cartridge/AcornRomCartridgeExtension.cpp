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

#include "AcornRomCartridgeExtension.hpp"

#include <beebium/extension/CartridgeSlot.hpp>
#include <beebium/extension/ExtensionContext.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace beebium {

namespace {

constexpr std::size_t BANK_SIZE = 16384;

}  // namespace

void AcornRomCartridgeExtension::init(ExtensionContext& ctx) {
    slot_ = &ctx.get<CartridgeSlot>();
    // The ROMs in its sockets are the machine's sideways_bank entries for
    // the slot's banks, which the server has read; a socket with none is an
    // empty bank.
    const auto& bank_images = slot_->bank_images();
    for (std::size_t i = 0; i < bank_images.size(); ++i) {
        const auto& bank_image = bank_images[i];
        if (bank_image.data.size() > BANK_SIZE) {
            const int bank = slot_->first_bank() + static_cast<int>(i);
            throw std::runtime_error("The ROM for bank " + std::to_string(bank) + ", " +
                                     bank_image.filepath + ", is " +
                                     std::to_string(bank_image.data.size()) +
                                     " bytes; a socket of the Acorn ROM Cartridge takes at "
                                     "most 16K.");
        }
        images_[i] = bank_image.data;
        names_[i] = bank_image.data.empty() ? std::string{} : bank_image.filepath;
    }
    slot_->fit(*this);
}

void AcornRomCartridgeExtension::shutdown() {
    if (slot_) {
        slot_->remove();
        slot_ = nullptr;
    }
}

SlotInfo AcornRomCartridgeExtension::bank_info(uint8_t bank) const {
    // An unfitted socket is an empty bank: the stale bus, no image.
    if (image(bank).empty()) return {SlotType::Empty, false, "", false};
    return {SlotType::Rom, true, image_name(bank), false};
}

}  // namespace beebium

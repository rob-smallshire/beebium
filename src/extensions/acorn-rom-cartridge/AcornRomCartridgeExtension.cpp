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

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace beebium {

namespace {

constexpr std::size_t BANK_SIZE = 16384;

// A cartridge ROM image: at most one bank.
std::vector<uint8_t> read_rom(const std::string& filepath, std::string_view which) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("The Acorn ROM Cartridge's " + std::string(which) + " image " +
                                 filepath + " could not be read.");
    }
    std::vector<uint8_t> data{std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>()};
    if (data.empty() || data.size() > BANK_SIZE) {
        throw std::runtime_error("The Acorn ROM Cartridge's " + std::string(which) + " image " +
                                 filepath + " is " + std::to_string(data.size()) +
                                 " bytes; a cartridge ROM is at most 16K.");
    }
    return data;
}

}  // namespace

void AcornRomCartridgeExtension::init(ExtensionContext& ctx) {
    names_[0] = std::string(config_value("low").value_or(""));
    names_[1] = std::string(config_value("high").value_or(""));
    if (names_[0].empty() && names_[1].empty()) {
        throw std::runtime_error(
            "An Acorn ROM Cartridge needs a ROM in at least one of its sockets: give low, high or both.");
    }
    if (!names_[0].empty()) images_[0] = read_rom(names_[0], "low");
    if (!names_[1].empty()) images_[1] = read_rom(names_[1], "high");
    slot_ = &ctx.get<CartridgeSlot>();
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

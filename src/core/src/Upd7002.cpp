// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/devices/Upd7002.hpp>

namespace beebium {

void Upd7002::power_on() {
    control_ = 0;
    result_ = 0;
    pending_result_ = 0;
    clocks_remaining_ = 0;
    eoc_ = false;
}

uint8_t Upd7002::status() const {
    uint8_t value = control_ & CONTROL_MASK;
    value |= static_cast<uint8_t>((result_ >> 14) << 4);
    if (!busy()) {
        value |= 0x40;
    }
    if (!eoc_) {
        value |= 0x80;
    }
    return value;
}

uint8_t Upd7002::peek(uint16_t offset) const {
    switch (offset & 0x03) {
    case 0:
        return status();
    case 1:
        return static_cast<uint8_t>(result_ >> 8);
    default:
        return static_cast<uint8_t>(result_ & 0xF0);
    }
}

uint8_t Upd7002::read(uint16_t offset) {
    const uint8_t value = peek(offset);
    if ((offset & 0x03) == 1) {
        eoc_ = false;
    }
    return value;
}

void Upd7002::write(uint16_t offset, uint8_t value) {
    if ((offset & 0x03) == 0) {
        start_conversion(value);
    }
    // Offsets 1 and 2 are not decoded for writing; offset 3 is the test
    // mode, which is not modelled.
}

void Upd7002::start_conversion(uint8_t value) {
    control_ = value & CONTROL_MASK;
    const uint16_t level = input_levels_[control_ & 0x03];
    const uint16_t code12 = static_cast<uint16_t>(level >> 4);
    if (twelve_bit()) {
        pending_result_ = static_cast<uint16_t>(code12 << 4);
    } else {
        pending_result_ = static_cast<uint16_t>(level & 0xFF00);
    }
    clocks_remaining_ = upd7002_timing::conversion_clocks(twelve_bit(), code12);
    eoc_ = false;
}

void Upd7002::complete_conversion() {
    result_ = pending_result_;
    eoc_ = true;
}

void Upd7002::tick_falling() {
    if (clocks_remaining_ != 0 && --clocks_remaining_ == 0) {
        complete_conversion();
    }
}

void Upd7002::set_input_level(int channel, uint16_t level) {
    input_levels_[static_cast<size_t>(channel & 0x03)] = level;
}

uint16_t Upd7002::input_level(int channel) const {
    return input_levels_[static_cast<size_t>(channel & 0x03)];
}

}  // namespace beebium

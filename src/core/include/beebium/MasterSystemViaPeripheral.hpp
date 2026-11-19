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

#pragma once

#include "AddressableLatch.hpp"
#include "SystemViaPeripheral.hpp"
#include "devices/Mc146818Rtc.hpp"
#include "indicators/Indicators.hpp"

#include <cstdint>

namespace beebium {

// The Master 128's System VIA: the Model B's sound and latch, its keyboard
// widened to 13 columns for the keypad and without start-up links, and the
// MC146818 clock in the speech chip's place.
//
// - Port A is the clock's multiplexed address and data bus, shared with the
//   keyboard and the sound chip.
// - PB6 is the clock's chip enable (CE) and PB7 its address strobe (AS),
//   driven directly by port B.
// - Addressable latch bit 1 (the B's speech read) is R/W, high to read, and
//   bit 2 (the B's speech write) is the data strobe DS.
//
// The bus protocol is the datasheet's Motorola mode (docs/datasheets/
// MC146818.md, the bus section), selected because DS is low when AS falls:
// the address latches on the falling edge of AS; a read drives the bus while
// DS is high with R/W high; a write latches the bus on the falling edge of DS
// with R/W low. CE gates both strobes: it must be high across them. The MOS
// sequence (New Advanced User Guide 19.6) is: AS high, the register number
// on port A, CE high, AS low; then R/W, DS high, the data, DS low; CE low.
class MasterSystemViaPeripheral : public SystemViaPeripheral {
public:
    static constexpr uint8_t PB_CE = 0x40;
    static constexpr uint8_t PB_AS = 0x80;
    static constexpr uint8_t LATCH_RW = AddressableLatch::SPEECH_READ;
    static constexpr uint8_t LATCH_DS = AddressableLatch::SPEECH_WRITE;

    MasterSystemViaPeripheral(AddressableLatch& latch, Indicators& indicators, Mc146818Rtc& rtc)
        : SystemViaPeripheral(latch, indicators, KeyboardMatrix::MASTER_128_LAYOUT), rtc_(rtc) {}

    uint8_t update_port_a(uint8_t output, uint8_t ddr) override {
        const uint8_t keyboard = SystemViaPeripheral::update_port_a(output, ddr);
        port_a_pins_ = pins(output, ddr);
        // While the clock drives the bus it is the only driver: the MOS
        // makes port A an input and leaves the keyboard alone for the read.
        return clock_driving_ ? clock_data_ : keyboard;
    }

    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        const uint8_t input = SystemViaPeripheral::update_port_b(output, ddr);
        const uint8_t pb = pins(output, ddr);
        const bool ce = (pb & PB_CE) != 0;
        const bool as = (pb & PB_AS) != 0;
        const uint8_t latch_value = latch().value;
        const bool read = (latch_value & LATCH_RW) != 0;
        const bool ds = (latch_value & LATCH_DS) != 0;

        if (ce) {
            if (as_ && !as) {
                rtc_.write_address(port_a_pins_);
            }
            if (!ds_ && ds && read) {
                clock_data_ = rtc_.read_data();
            }
            if (ds_ && !ds && !read) {
                rtc_.write_data(port_a_pins_);
            }
        }
        clock_driving_ = ce && ds && read;
        as_ = as;
        ds_ = ds;
        return input;
    }

    // Whether the clock is driving port A (a read in progress).
    bool clock_driving() const { return clock_driving_; }

private:
    // The levels on a port's pins: the output register where the pin is an
    // output, high (the pull-ups) where it is an input.
    static uint8_t pins(uint8_t output, uint8_t ddr) {
        return static_cast<uint8_t>((output & ddr) | ~ddr);
    }

    Mc146818Rtc& rtc_;
    uint8_t port_a_pins_ = 0xFF;
    uint8_t clock_data_ = 0xFF;
    bool clock_driving_ = false;
    bool as_ = false;
    bool ds_ = false;
};

}  // namespace beebium

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

#ifndef BEEBIUM_ANALOGUE_PORT_DEVICE_HPP
#define BEEBIUM_ANALOGUE_PORT_DEVICE_HPP

#include "Export.hpp"

#include <cstdint>

namespace beebium {

// Interface for a device on the BBC Micro's 15-way analogue port.
//
// The connector carries the uPD7002's four channel inputs, VREF and
// analogue ground, two button lines (System VIA PB4 and PB5, which the
// connector calls PB0 and PB1) and the light pen strobe LPSTB (System VIA
// CB2 and the 6845's light pen input). A device sees those and only those:
// never the VIA's other lines or the converter's registers.
//
// A joystick drives CH0 and CH1 (stick 1) and CH2 and CH3 (stick 2) and
// grounds a button line when its fire button is pressed; a light pen drives
// LPSTB and uses a button line as its switch. The connector takes one plug,
// so the port has one device; a pair of joysticks is one device.
struct BEEBIUM_EXT_API AnaloguePortDevice {
    virtual ~AnaloguePortDevice() = default;

    // The level the device drives on channel 0 to 3, as a 16-bit fraction
    // of VREF: 0 is analogue ground, &FFFF is VREF. The converter calls this
    // when it samples at the start of a conversion, so during emulation it
    // is read at most once per conversion and never on a tick; inspecting
    // the converter's state (the debugger) also reads it, outside any
    // conversion. A channel the device leaves unconnected returns
    // UNDRIVEN_LEVEL.
    virtual uint16_t channel_level(int channel) = 0;

    // The levels the device drives on the two button lines, in place as the
    // System VIA reads them: bit 4 for PB4 (connector PB0) and bit 5 for
    // PB5 (connector PB1), 1 = not pressed (the pull-up), 0 = pressed. Other
    // bits are ignored. Called whenever the System VIA updates port B.
    virtual uint8_t button_lines() { return 0x30; }

    // The level the device drives on LPSTB: 1 while the strobe is asserted,
    // 0 otherwise. Called on every System VIA tick and every CRTC character
    // clock; the VIA's own edge detector and the CRTC's own latch see the
    // transitions.
    virtual uint8_t light_pen_strobe() { return 0; }

    // The level of an input nothing drives: the centre of the range.
    static constexpr uint16_t UNDRIVEN_LEVEL = 0x8000;
};

}  // namespace beebium

#endif  // BEEBIUM_ANALOGUE_PORT_DEVICE_HPP

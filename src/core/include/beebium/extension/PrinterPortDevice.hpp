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


#ifndef BEEBIUM_PRINTER_PORT_DEVICE_HPP
#define BEEBIUM_PRINTER_PORT_DEVICE_HPP

#include "Export.hpp"

#include <cstdint>

namespace beebium {

// Interface for a device on the BBC Micro's parallel printer connector.
//
// The printer port is the User VIA's Port A, buffered to outputs only by a
// 74LS244, with CA2 as /STROBE out and CA1 as /ACK in. A device sees the
// eight data lines and the strobe level, and drives the acknowledge level.
// It cannot drive the data lines and never sees Port B or CB1/CB2, which
// belong to the User Port.
//
// Only one PrinterPortDevice can be attached at a time: the connector is a
// single Centronics socket.
struct BEEBIUM_EXT_API PrinterPortDevice {
    virtual ~PrinterPortDevice() = default;

    // The eight data lines after the buffer, called whenever port A's
    // output register or DDR changes the lines. Bits whose DDR is input
    // float high through the buffer. Most devices ignore this and read the
    // data at the strobe instead.
    virtual void update_data_lines(uint8_t data) { (void)data; }

    // Called on every VIA tick (1 MHz), and additionally whenever the CPU
    // writes a port A register, so a device counting ticks sees at most one
    // extra call per register write. strobe is the level of /STROBE as the
    // VIA drives it (CA2): 0 while asserted. data is the current level of
    // the data lines. The device returns the level it drives on /ACK (CA1):
    // 1 when idle (the pull-up), 0 while asserted. The VIA resets CA1 to 1
    // after each tick, so the device must return its level on every call;
    // the VIA's own edge detector raises the CA1 interrupt.
    virtual uint8_t update_control_lines(uint8_t strobe, uint8_t data) = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_PRINTER_PORT_DEVICE_HPP

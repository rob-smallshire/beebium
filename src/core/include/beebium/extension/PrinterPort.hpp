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


#ifndef BEEBIUM_EXTENSION_PRINTER_PORT_HPP
#define BEEBIUM_EXTENSION_PRINTER_PORT_HPP

#include "Export.hpp"
#include "PrinterPortDevice.hpp"

#include <concepts>

namespace beebium {

class UserViaPeripheral;

// Port handle for the BBC Micro's parallel printer port.
//
// A thin handle over the hardware-owned UserViaPeripheral composite, which
// is the ViaPeripheral installed on the User VIA. Attaching a device here
// places it on the composite's Port A / CA1 / CA2 half; the device never
// sees Port B or CB1/CB2, which belong to the User Port.
//
// Only one device can be attached at a time -- the printer port is a
// single Centronics connector. Attempting to attach a second device throws.
class BEEBIUM_EXT_API PrinterPort {
public:
    explicit PrinterPort(UserViaPeripheral& user_via_peripheral);

    // Attach a device to the printer port.
    // Throws std::runtime_error if a device is already attached.
    void attach(PrinterPortDevice& device);

    // Query whether a device is currently attached.
    bool is_occupied() const;

private:
    UserViaPeripheral& user_via_peripheral_;
};

// Concept to detect hardware that has a printer port.
template<typename T>
concept HasPrinterPort = requires(T& hw) {
    { hw.printer_port() } -> std::convertible_to<PrinterPort&>;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_PRINTER_PORT_HPP

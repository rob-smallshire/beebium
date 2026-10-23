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

#include "PrinterDevice.hpp"

#include <cctype>

namespace beebium::printer {

std::string win32_device_path(const std::string& device) {
    const bool bare_lpt =
        device.size() > 3 &&
        std::toupper(static_cast<unsigned char>(device[0])) == 'L' &&
        std::toupper(static_cast<unsigned char>(device[1])) == 'P' &&
        std::toupper(static_cast<unsigned char>(device[2])) == 'T' &&
        device.find_first_not_of("0123456789", 3) == std::string::npos;
    return bare_lpt ? "\\\\.\\" + device : device;
}

}  // namespace beebium::printer

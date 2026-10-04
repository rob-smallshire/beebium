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

// A clock frequency as people write it: "3 MHz", "3.5 MHz".

#ifndef BEEBIUM_CLOCK_TEXT_HPP
#define BEEBIUM_CLOCK_TEXT_HPP

#include <cstdint>
#include <string>

namespace beebium {

// `hz` in megahertz: whole megahertz as "3 MHz", otherwise the shortest
// decimal that is exact to the hertz ("3.5 MHz", "4.194304 MHz").
inline std::string format_clock_mhz(uint64_t hz) {
    const uint64_t whole = hz / 1'000'000;
    uint64_t fraction = hz % 1'000'000;
    std::string text = std::to_string(whole);
    if (fraction != 0) {
        std::string digits = std::to_string(fraction);
        digits.insert(0, 6 - digits.size(), '0');
        digits.erase(digits.find_last_not_of('0') + 1);
        text += "." + digits;
    }
    return text + " MHz";
}

}  // namespace beebium

#endif  // BEEBIUM_CLOCK_TEXT_HPP

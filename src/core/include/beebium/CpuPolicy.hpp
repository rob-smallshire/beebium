// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_CPU_POLICY_HPP
#define BEEBIUM_CPU_POLICY_HPP

#include <6502/6502.h>

namespace beebium {

// CPU policies select the 6502 variant at compile time.
// Each policy provides a static config pointer to the M6502Config.

struct Nmos6502 {
    static constexpr const M6502Config* config = &M6502_nmos6502_config;
    static constexpr const char* name = "NMOS 6502";
};

struct Cmos65C02 {
    static constexpr const M6502Config* config = &M6502_cmos6502_config;
    static constexpr const char* name = "CMOS 65C02";
};

struct Rockwell65C02 {
    static constexpr const M6502Config* config = &M6502_rockwell65c02_config;
    static constexpr const char* name = "Rockwell 65C02";
};

} // namespace beebium

#endif // BEEBIUM_CPU_POLICY_HPP

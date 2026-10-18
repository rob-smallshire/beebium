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

#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>
#include <vector>

namespace beebium {

// A named value a debugger condition can test, read from the machine's own
// latches -- write-only ones included, which mem[] cannot see -- without side
// effects. A hardware policy supplies its own (debug_variables()), so the
// debugger names no latch itself. Names are lower case, so they never collide
// with the CPU's upper-case registers.
struct DebugVariable {
    std::string_view name;
    std::string_view description;  // one line
    std::function<uint64_t()> read;
    // About the access a watchpoint is judging (shadow_access), so only for
    // watchpoint conditions.
    bool watchpoint_only = false;
};

// The access a watchpoint judges, which its conditions can name too, on every
// machine: the debugger supplies these, not the hardware.
struct AccessVariable {
    std::string_view name;
    std::string_view description;  // one line
};
inline constexpr AccessVariable WATCHPOINT_ACCESS_VARIABLES[] = {
    {"addr", "The CPU address of the access."},
    {"value", "The byte read or written."},
    {"write", "1 for a write, 0 for a read."},
};

// The pair every model has: the selected sideways bank and the whole ROMSEL
// latch (&FE30), each as last written, in the bits the hardware keeps.
inline std::vector<DebugVariable> romsel_variables(std::function<uint64_t()> romsel) {
    auto bank = [romsel] { return romsel() & 0x0F; };
    return {
        {"bank", "The sideways bank selected: ROMSEL (&FE30) bits 0-3 as last written.",
         std::move(bank)},
        {"romsel", "The ROMSEL latch (&FE30) as last written, in the bits the hardware keeps; "
                   "it is write-only, so mem[] cannot read it.",
         std::move(romsel)},
    };
}

}  // namespace beebium

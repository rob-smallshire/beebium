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

// A slice of the server's canonical key table (src/core/src/KeyboardMapping.cpp)
// for the keyboard tests: the same key numbers, names and SHIFT faces.

#ifndef BEEBIUM_LINUX_TESTS_SAMPLE_KEY_TABLE_HPP
#define BEEBIUM_LINUX_TESTS_SAMPLE_KEY_TABLE_HPP

#include "keyboard/BbcKeyTable.hpp"

namespace beebium::linux_client::testing {

inline BbcKeyTable sample_key_table() {
    return BbcKeyTable({
        {"", 0x00, false, "Shift"},
        {"", 0x01, false, "Ctrl"},
        {"", 0x50, false, "Shift Lock"},
        {"", 0x70, false, "Escape"},
        {"", 0x59, false, "Delete"},
        {"", 0x69, false, "Copy"},
        {"", 0x39, false, "Up"},
        {"", 0x71, false, "f1"},
        {"", 0x20, false, "f0"},
        {"a", 0x41, false, "a"},
        {"h", 0x54, false, "h"},
        {"m", 0x65, false, "m"},
        {"A", 0x41, true, "A"},
        {"M", 0x65, true, "M"},
        {"H", 0x54, true, "H"},
        {"7", 0x24, false, "7"},
        {"'", 0x24, true, "'"},
        {";", 0x57, false, ";"},
        {"+", 0x57, true, "+"},
        {":", 0x48, false, ":"},
        {"*", 0x48, true, "*"},
        {"\r", 0x49, false, "\\r"},
        {" ", 0x62, false, "Space"},
    });
}

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_SAMPLE_KEY_TABLE_HPP

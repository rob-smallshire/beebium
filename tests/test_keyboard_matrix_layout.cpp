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

// test_keyboard_matrix_layout.cpp
// A keyboard's layout (KeyboardMatrix::Layout): the Model B's ten columns
// with start-up links, and the Master 128's thirteen, its keypad in columns
// 10-12, with none. The keypad's keys in the catalogue (KeyboardMapping) at
// the Master's internal key numbers (research section 1.5).

#include <beebium/KeyboardMapping.hpp>
#include <beebium/KeyboardMatrix.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>

using namespace beebium;

TEST_CASE("A Model B keyboard has ten columns and its start-up links",
          "[keyboard][matrix][layout]") {
    KeyboardMatrix keyboard;
    CHECK(keyboard.column_count() == 10);
    CHECK(keyboard.has_links());
    keyboard.key_down(6, 10);  // the Master's keypad 0: no such column
    CHECK_FALSE(keyboard.is_key_pressed(6, 10));
    CHECK(keyboard.read_column(10) == 0);
    keyboard.set_startup_options(0x00);
    CHECK(keyboard.is_link_made(9));
}

TEST_CASE("A Master 128 keyboard has thirteen columns and no start-up links",
          "[keyboard][matrix][layout][master128]") {
    KeyboardMatrix keyboard(KeyboardMatrix::MASTER_128_LAYOUT);
    CHECK(keyboard.column_count() == 13);
    CHECK_FALSE(keyboard.has_links());
    for (uint8_t column = 10; column <= 12; ++column) {
        keyboard.key_down(7, column);
        CHECK(keyboard.is_key_pressed(7, column));
        CHECK(keyboard.any_key_in_column(column, /*exclude_row_0=*/true));
        keyboard.key_up(7, column);
        CHECK_FALSE(keyboard.is_key_pressed(7, column));
    }
    keyboard.key_down(1, 13);  // past the keypad
    CHECK_FALSE(keyboard.is_key_pressed(1, 13));
    keyboard.key_down(3, 12);
    CHECK(keyboard.get_row_state(3) == (1u << 12));
    // No links, whatever the options byte says.
    keyboard.set_startup_options(0x00);
    for (uint8_t column = 2; column <= 9; ++column) CHECK_FALSE(keyboard.is_link_made(column));
}

TEST_CASE("The keypad's keys are in the catalogue at the Master's internal numbers",
          "[keyboard][mapping][master128]") {
    const std::pair<const char*, uint8_t> keypad[] = {
        {"Keypad0", 0x6A}, {"Keypad1", 0x6B}, {"Keypad2", 0x7C}, {"Keypad3", 0x6C},
        {"Keypad4", 0x7A}, {"Keypad5", 0x7B}, {"Keypad6", 0x1A}, {"Keypad7", 0x1B},
        {"Keypad8", 0x2A}, {"Keypad9", 0x2B}, {"KeypadPlus", 0x3A}, {"KeypadMinus", 0x3B},
        {"KeypadDivide", 0x4A}, {"KeypadHash", 0x5A}, {"KeypadMultiply", 0x5B},
        {"KeypadComma", 0x5C}, {"KeypadPeriod", 0x4C}, {"KeypadReturn", 0x3C},
        {"KeypadDelete", 0x4B},
    };
    for (const auto& [name, ik] : keypad) {
        INFO(name);
        const auto mapping = key_by_name(name);
        REQUIRE(mapping);
        CHECK(mapping->ik_number == ik);
        CHECK(mapping->is_named_only());
        CHECK(std::string(key_name(ik)) == name);
    }
    // Typing a digit is the main keyboard's key, never the keypad's.
    CHECK(char_to_key('1')->ik_number == 0x30);
    CHECK(char_to_key('+')->column() < 10);
}

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

#include "keyboard/EvdevCodes.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("Common evdev codes map to their W3C code values", "[keyboard][evdev]") {
    CHECK(w3c_code_for_evdev(1) == "Escape");
    CHECK(w3c_code_for_evdev(30) == "KeyA");
    CHECK(w3c_code_for_evdev(44) == "KeyZ");
    CHECK(w3c_code_for_evdev(11) == "Digit0");
    CHECK(w3c_code_for_evdev(28) == "Enter");
    CHECK(w3c_code_for_evdev(42) == "ShiftLeft");
    CHECK(w3c_code_for_evdev(54) == "ShiftRight");
    CHECK(w3c_code_for_evdev(29) == "ControlLeft");
    CHECK(w3c_code_for_evdev(97) == "ControlRight");
    CHECK(w3c_code_for_evdev(103) == "ArrowUp");
    CHECK(w3c_code_for_evdev(108) == "ArrowDown");
    CHECK(w3c_code_for_evdev(59) == "F1");
    CHECK(w3c_code_for_evdev(68) == "F10");
    CHECK(w3c_code_for_evdev(87) == "F11");
    CHECK(w3c_code_for_evdev(88) == "F12");
    CHECK(w3c_code_for_evdev(86) == "IntlBackslash");
}

TEST_CASE("An evdev code with no neutral code maps to nothing", "[keyboard][evdev]") {
    CHECK(w3c_code_for_evdev(0).empty());
    CHECK(w3c_code_for_evdev(9999).empty());
}

TEST_CASE("Codes round-trip through the table", "[keyboard][evdev]") {
    for (std::uint32_t evdev = 0; evdev < 256; ++evdev) {
        const auto code = w3c_code_for_evdev(evdev);
        if (!code.empty()) {
            CHECK(evdev_for_w3c_code(code) == evdev);
        }
    }
    CHECK_FALSE(evdev_for_w3c_code("NoSuchKey").has_value());
}

TEST_CASE("Qt's native scan code is the XKB keycode, evdev plus 8", "[keyboard][evdev]") {
    CHECK(evdev_for_xkb_keycode(38) == 30u);  // A
    CHECK(evdev_for_xkb_keycode(8) == 0u);
    CHECK_FALSE(evdev_for_xkb_keycode(0).has_value());
    CHECK_FALSE(evdev_for_xkb_keycode(7).has_value());
}

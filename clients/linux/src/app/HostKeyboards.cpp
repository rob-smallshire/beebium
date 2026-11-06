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

#include "app/HostKeyboards.hpp"

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>

namespace beebium::linux_client {

namespace {

constexpr const char* INPUT_DEVICES_FILEPATH = "/proc/bus/input/devices";

std::optional<KeyboardClasses>& classes_held() {
    static std::optional<KeyboardClasses> classes;
    return classes;
}

KeyboardClasses read_host_keyboard_classes() {
    std::ostringstream text;
    if (std::ifstream devices{INPUT_DEVICES_FILEPATH}) {
        text << devices.rdbuf();
    }
    const auto keyboards = keyboards_in_input_devices(text.str());
    // Named in the log, so a keyboard not yet classed (a Raspberry Pi 500's,
    // say) can be recognised from what a user reports.
    std::fprintf(stderr, "beebium: keyboards: %s\n", keyboards_description(keyboards).c_str());
    return keyboard_classes(keyboards);
}

}  // namespace

const KeyboardClasses& host_keyboard_classes() {
    auto& classes = classes_held();
    if (!classes) {
        classes = read_host_keyboard_classes();
    }
    return *classes;
}

void set_host_keyboard_classes(KeyboardClasses classes) {
    classes_held() = std::move(classes);
}

}  // namespace beebium::linux_client

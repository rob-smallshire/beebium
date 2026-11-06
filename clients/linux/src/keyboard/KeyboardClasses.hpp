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

#ifndef BEEBIUM_LINUX_KEYBOARD_KEYBOARD_CLASSES_HPP
#define BEEBIUM_LINUX_KEYBOARD_KEYBOARD_CLASSES_HPP

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::linux_client {

// The classes of keyboard attached to the host, as a mapping entry's
// when.keyboard names them (docs/keyboard-mapping-files.md 3.5):
// "standard" and "pi-internal".
using KeyboardClasses = std::set<std::string>;

// A typing keyboard among the host's input devices.
struct InputKeyboard {
    std::string name;
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;
    std::string keyboard_class;
};

// A keyboard's class: "pi-internal" for a name containing "Raspberry Pi" and
// "Internal Keyboard", ignoring case, or the Raspberry Pi 400's own ids
// (vendor 04d9, product 0007); "standard" for any other.
std::string keyboard_class_of(std::string_view name, std::uint16_t vendor, std::uint16_t product);

// The typing keyboards in the text of /proc/bus/input/devices: devices the
// kbd handler serves that have the letter keys, which leaves out power
// buttons, HDMI remote controls and a keyboard's media-key interfaces.
std::vector<InputKeyboard> keyboards_in_input_devices(std::string_view devices_text);

// The classes the keyboards make up; with none to read, a standard keyboard
// is taken to be attached.
KeyboardClasses keyboard_classes(const std::vector<InputKeyboard>& keyboards);

// One line naming each keyboard with its class and ids, for the log.
std::string keyboards_description(const std::vector<InputKeyboard>& keyboards);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_KEYBOARD_CLASSES_HPP

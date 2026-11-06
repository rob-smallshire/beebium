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

#ifndef BEEBIUM_LINUX_APP_HOST_KEYBOARDS_HPP
#define BEEBIUM_LINUX_APP_HOST_KEYBOARDS_HPP

#include "keyboard/KeyboardClasses.hpp"

namespace beebium::linux_client {

// The classes of keyboard attached to this host, which a mapping entry's
// when.keyboard is decided against (docs/keyboard-mapping-files.md 3.5).
// Read from /proc/bus/input/devices at the first call, which logs every
// keyboard found with its class and ids; keyboards that come and go later
// are not followed.
const KeyboardClasses& host_keyboard_classes();

// For tests: the classes host_keyboard_classes() gives from now on.
void set_host_keyboard_classes(KeyboardClasses classes);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_HOST_KEYBOARDS_HPP

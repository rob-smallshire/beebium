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

#ifndef BEEBIUM_LINUX_KEYBOARD_EVDEV_CODES_HPP
#define BEEBIUM_LINUX_KEYBOARD_EVDEV_CODES_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace beebium::linux_client {

// Linux physical key identity to the platform-neutral W3C UI Events `code`
// value ("KeyZ", "ShiftLeft") that keyboard mapping files key on (client
// specification 7.4). This table is the client's only knowledge of Linux key
// numbering; everything after it speaks neutral codes.

// The code for an evdev key code (linux/input-event-codes.h); empty for one
// with no neutral code.
std::string_view w3c_code_for_evdev(std::uint32_t evdev_code);

// The inverse, for tests and for presenting a mapping; nullopt if unknown.
std::optional<std::uint32_t> evdev_for_w3c_code(std::string_view code);

// Qt's QKeyEvent::nativeScanCode() on both xcb (X11) and Wayland is the XKB
// keycode, which is the evdev code plus 8. Zero (a synthesised event) and
// anything below 8 have no physical identity.
std::optional<std::uint32_t> evdev_for_xkb_keycode(std::uint32_t xkb_keycode);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_EVDEV_CODES_HPP

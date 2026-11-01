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

#ifndef BEEBIUM_LINUX_KEYBOARD_CAPS_LOCK_SYNC_HPP
#define BEEBIUM_LINUX_KEYBOARD_CAPS_LOCK_SYNC_HPP

#include <chrono>
#include <cstdint>
#include <optional>

namespace beebium::linux_client {

// Caps Lock synchronisation, host to guest (client specification 7.3).
//
// The machine owns its Caps Lock latch and re-initialises it on every reset;
// the client only nudges it to match the host, and only when the two
// disagree, so it never fights the MOS. Caps Lock is a toggle with its own
// path, never a held key: the guest latch is tapped with a down, a hold long
// enough for the MOS to scan it (it toggles on key-down only), and an up.

inline constexpr std::uint8_t BBC_CAPS_LOCK_IK = 0x40;
inline constexpr std::chrono::milliseconds CAPS_LOCK_TAP_HOLD{50};

// Tap the guest's Caps Lock? Only when synchronisation is on, both states
// are known exactly (the guest's from KeyboardService.GetLockState, never
// the duty-cycle-filtered LED), and they differ.
inline bool should_tap_caps_lock(bool sync_enabled, std::optional<bool> host_on,
                                 std::optional<bool> guest_on) {
    return sync_enabled && host_on && guest_on && *host_on != *guest_on;
}

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_CAPS_LOCK_SYNC_HPP

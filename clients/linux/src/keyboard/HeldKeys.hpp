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

#ifndef BEEBIUM_LINUX_KEYBOARD_HELD_KEYS_HPP
#define BEEBIUM_LINUX_KEYBOARD_HELD_KEYS_HPP

#include "keyboard/KeyResolver.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace beebium::linux_client {

// What reaches the server, in this order.
enum class WireAction {
    key_down,
    key_up,
    break_down,
    break_up,
    // Align the guest's Caps Lock to caps_lock_on, tapping it only if the
    // guest's exact latch differs (CapsLockSync.hpp).
    caps_lock_sync,
};

struct WireEvent {
    WireAction action = WireAction::key_down;
    std::uint8_t ik_number = 0;  // matrix keys only
    bool caps_lock_on = false;   // caps_lock_sync only

    bool operator==(const WireEvent&) const = default;
};

// The held-key set and the BBC SHIFT and CTRL derived from it
// (frontend-modifier-keys.md section 1, R1 to R10; client specification
// 7.2). Pure: it turns host presses and releases into the wire events to
// send, and the caller sends them, in order, through one queue.
//
// Each press's modifier facts are decided when it is pressed and frozen. BBC
// SHIFT and CTRL are never passed through: each is derived over the whole set
// with forbid over need over the user's own modifier, and only transitions
// are sent. On a press the modifiers go before the key; on a release the key
// goes before the modifiers.
class HeldKeys {
public:
    // A physical identity broad enough for every input path (R10):
    // "evdev:30" for the keyboard, "bbc:Break" for an on-screen key.
    using KeyId = std::string;

    // A press of `id`. A repeat press of a key already held sends nothing
    // (R2, R7).
    std::vector<WireEvent> press(const KeyId& id, const ResolvedKey& key);

    // A release of `id`. A release of a key not held sends nothing (R8).
    std::vector<WireEvent> release(const KeyId& id);

    // Release everything, as on focus loss or a host takeover (R4, R5). The
    // caller must send the result synchronously, before anything else.
    std::vector<WireEvent> release_all();

    bool is_held(const KeyId& id) const { return held_.count(id) != 0; }
    bool empty() const { return held_.empty(); }
    bool bbc_shift_down() const { return sent_shift_; }
    bool bbc_ctrl_down() const { return sent_ctrl_; }

private:
    struct Facts {
        bool needs_shift = false;
        bool forbids_shift = false;
        bool needs_ctrl = false;
        bool forbids_ctrl = false;
    };
    struct Held {
        ResolvedKey key;
        Facts facts;
    };

    bool physical(KeyRole role) const;
    Facts facts_for(const ResolvedKey& key) const;
    void reconcile(std::vector<WireEvent>& events);

    std::map<KeyId, Held> held_;
    bool sent_shift_ = false;
    bool sent_ctrl_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_KEYBOARD_HELD_KEYS_HPP

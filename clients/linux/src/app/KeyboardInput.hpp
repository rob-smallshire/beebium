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

#ifndef BEEBIUM_LINUX_APP_KEYBOARD_INPUT_HPP
#define BEEBIUM_LINUX_APP_KEYBOARD_INPUT_HPP

#include "keyboard/BbcKeyTable.hpp"
#include "keyboard/HeldKeys.hpp"
#include "keyboard/KeyResolver.hpp"

#include <QKeyEvent>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// The physical identity and characters of a Qt key event, as the resolver
// needs them. Nullopt for an event with no physical key (synthesised, or a
// key the evdev table does not know).
std::optional<HostKey> host_key_from_event(const QKeyEvent& event);

// The keyboard path of one machine window: Qt key events in, wire events out
// through `send`, on the GUI thread (client specification 7.1, 7.2).
class KeyboardInput {
public:
    using Sender = std::function<void(const std::vector<WireEvent>&)>;

    // Connected: resolve against this server's table and send with `send`.
    void attach(BbcKeyTable table, Sender send);

    // Disconnected: release what is held, then send nothing more.
    void detach();

    // Return true if the event was the machine's.
    bool key_press(const QKeyEvent& event);
    bool key_release(const QKeyEvent& event);

    // Focus loss or a host takeover: release everything, now (R4, R5).
    void release_all();

    const HeldKeys& held_keys() const { return held_; }

private:
    KeyMapping mapping_ = logical_key_mapping();
    BbcKeyTable table_;
    Sender send_;
    HeldKeys held_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_KEYBOARD_INPUT_HPP

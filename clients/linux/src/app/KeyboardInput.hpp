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
#include "keyboard/MappingFile.hpp"

#include <QKeyEvent>
#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// The physical identity and character of a Qt key event, as resolution needs
// them, given the host modifiers held besides it. Nullopt for an event with
// no physical key (synthesised).
std::optional<HostKey> host_key_from_event(const QKeyEvent& event, HostModifiers held);

// A built-in mapping (docs/keyboard-mapping-files.md 5), from the copies of
// the conformance corpus compiled into the client: "Default", "Logical", or
// a game's name. Nullopt for an unknown name.
std::optional<MappingFile> built_in_mapping(const QString& name);

// The keyboard path of one machine window: Qt key events in, wire events out
// through `send`, on the GUI thread (client specification 7.1, 7.2, 7.4).
class KeyboardInput {
public:
    using Sender = std::function<void(const std::vector<WireEvent>&)>;

    KeyboardInput();

    // Resolve with this mapping from now on. Keys already down are released
    // first, so none is left resolved by the old one.
    void set_mapping(MappingFile mapping);
    const MappingFile& mapping() const { return mapping_; }

    // Connected: resolve against this server's catalogue and send with `send`.
    void attach(BbcKeyTable table, Sender send);

    // Disconnected: release what is held, then send nothing more.
    void detach();

    // Return true if the event was the machine's.
    bool key_press(const QKeyEvent& event);
    bool key_release(const QKeyEvent& event);

    // Focus loss or a host takeover: release everything, now (R4, R5), and
    // forget which host modifiers were down; their releases go elsewhere.
    void release_all();

    // The host modifiers held now, from tracking (section 4).
    HostModifiers held_modifiers() const;

    // Caps Lock synchronisation (client specification 7.3), when the mapping
    // asks for it: where the host state comes from, and what to call when the
    // host Caps Lock key is pressed (its new state is not known until the
    // windowing system has applied the press, so the caller syncs later).
    using HostCapsLock = std::function<std::optional<bool>()>;
    void set_host_caps_lock(HostCapsLock host_caps_lock) { host_caps_lock_ = std::move(host_caps_lock); }
    void set_caps_lock_pressed(std::function<void()> pressed) { caps_lock_pressed_ = std::move(pressed); }

    // Align the guest's Caps Lock to the host's, if the mapping synchronises
    // it, the host state is known and the machine is attached. The guest is
    // tapped only if it differs.
    void sync_caps_lock();

    const HeldKeys& held_keys() const { return held_; }

private:
    MappingFile mapping_;
    BbcKeyTable table_;
    Sender send_;
    HeldKeys held_;
    // Host modifier keys down, by physical identity, whether or not the
    // mapping sends them to the machine.
    std::map<std::string, HostModifiers> host_modifiers_down_;
    HostCapsLock host_caps_lock_;
    std::function<void()> caps_lock_pressed_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_KEYBOARD_INPUT_HPP

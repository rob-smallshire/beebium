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
#include <string_view>
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
// the conformance corpus compiled into the client: "Logical", or a game's
// name. Nullopt for an unknown name.
std::optional<MappingFile> built_in_mapping(const QString& name);

// Where the keyboard's keys go now: to the machine while the display has
// focus and a machine is attached, else to the application (client
// specification 7.1, 13.2). Shown in the status bar.
enum class KeysGoTo { machine, application };

inline KeysGoTo keys_go_to(bool display_focused, bool machine_attached) {
    return display_focused && machine_attached ? KeysGoTo::machine : KeysGoTo::application;
}

// The keyboard path of one machine window: Qt key events in, wire events out
// through `send`, on the GUI thread (client specification 7.1, 7.2, 7.4).
class KeyboardInput {
public:
    using Sender = std::function<void(const std::vector<WireEvent>&)>;

    KeyboardInput();

    // Resolve with this mapping from now on. Keys already down are released
    // first, so none is left resolved by the old one, and the user's key
    // switches start again from the new mapping's defaults.
    void set_mapping(MappingFile mapping);
    const MappingFile& mapping() const { return mapping_; }

    // Connected: resolve against this server's catalogue and send with `send`.
    void attach(BbcKeyTable table, Sender send);

    // Disconnected: release what is held, then send nothing more.
    void detach();

    bool attached() const { return static_cast<bool>(send_); }

    // BREAK from a control rather than a key (5.5): held like any key, in
    // the one held-key set, so focus loss and detaching release it (R10).
    void press_break();
    void release_break();

    // A BBC key by its catalogue name ("Shift Lock", or "Break"), as a
    // control rather than a host key presses it: an LED's related key (10).
    // It goes in the one held-key set, keyed "<source>:<name>" so paths are
    // told apart, so release_all() covers it (rule R10); Break keeps its own
    // message. False when detached or the catalogue has no such key.
    bool press_named_key(std::string_view source, std::string_view name);
    void release_named_key(std::string_view source, std::string_view name);

    // Whether a key would go to the machine rather than to the application,
    // for deciding shortcut overrides. Not while detached; never F11 (full
    // screen); and not with Alt or Meta held unless the mapping binds this
    // key with one of them, Alt being the host's menu key here
    // (frontend-modifier-keys.md R5). A modifier key pressed with Alt held
    // is never the machine's: Alt is the selection modifier, and its chords
    // are the window's.
    bool takes(const QKeyEvent& event) const;
    // A key other than a modifier, pressed with Alt or Meta, that the mapping
    // does not bind with either: the window's. The BBC Micro has neither
    // key, so every application shortcut of the machine window carries one
    // (Shortcuts.hpp), and such a chord is never typed.
    bool window_chord(const QKeyEvent& event) const;
    // A window chord is about to fire: the CTRL and SHIFT that reached the
    // machine on their way down come up now, never left held. Which host
    // modifiers are physically down is kept: the chord may be pressed again.
    void yield_to_window();

    // A host gesture owns the qualifier modifiers (a screen text
    // selection): a modifier key's press or release is tracked, so later
    // keys resolve with what is physically down, but nothing reaches the
    // machine. False, and nothing tracked, for any other key.
    bool track_host_modifier(const QKeyEvent& event);

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
    // The user pressed a key the machine takes, or Break: called before it
    // is sent. Not for what the client sends of its own accord (lock
    // syncs, paste, releases).
    void on_user_press(std::function<void()> pressed) { user_pressed_ = std::move(pressed); }

    // Caps Lock synchronisation for this session, overriding the mapping's
    // default (the Keyboard mode's toggle); nullopt to follow the mapping.
    // Switching it on is a sync point (7.3).
    void set_caps_lock_sync_override(std::optional<bool> on);
    bool caps_lock_sync_enabled() const {
        return caps_lock_sync_override_.value_or(mapping_.synchronize_caps_lock);
    }

    // The keys the mapping lets the user switch off, by BBC key name in
    // name order, each with whether it is off now (the Keyboard mode's
    // Disable toggles, for this window's session). Switching affects the
    // next press: a key already down releases as usual.
    std::vector<std::pair<std::string, bool>> switchable_keys() const;
    void set_key_switched_off(const std::string& key, bool off);
    // A switched-off key was pressed: the desktop's bell by default.
    void on_switched_off_press(std::function<void()> pressed) {
        switched_off_pressed_ = std::move(pressed);
    }

    // Align the guest's Caps Lock to the host's, if the mapping synchronises
    // it, the host state is known and the machine is attached. The guest is
    // tapped only if it differs.
    void sync_caps_lock();

    const HeldKeys& held_keys() const { return held_; }

private:
    // Release the keys held on the machine, keeping track of the host's
    // modifiers, which are still down.
    void release_machine_keys();
    MappingFile mapping_;
    BbcKeyTable table_;
    Sender send_;
    std::function<void()> user_pressed_;
    HeldKeys held_;
    // Host modifier keys down, by physical identity, whether or not the
    // mapping sends them to the machine.
    std::map<std::string, HostModifiers> host_modifiers_down_;
    HostCapsLock host_caps_lock_;
    std::optional<bool> caps_lock_sync_override_;
    KeySwitches switches_;
    std::function<void()> switched_off_pressed_;
    std::function<void()> caps_lock_pressed_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_KEYBOARD_INPUT_HPP

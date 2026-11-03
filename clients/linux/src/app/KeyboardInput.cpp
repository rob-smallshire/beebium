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

#include "app/KeyboardInput.hpp"

#include "keyboard/EvdevCodes.hpp"

#include <QFile>

#include <algorithm>

namespace beebium::linux_client {

namespace {

// The character a key gives without CTRL. Qt reports Ctrl+M as text "\r",
// but key() still names M; letters come back as capitals, so take the case
// SHIFT selects (R9).
std::string character_ignoring_ctrl(const QKeyEvent& event, bool shift_held) {
    const int key = event.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return std::string(1, static_cast<char>((shift_held ? 'A' : 'a') + (key - Qt::Key_A)));
    }
    if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde) {
        return std::string(1, static_cast<char>(key));
    }
    return {};
}

HeldKeys::KeyId key_id(const QKeyEvent& event) {
    const auto evdev = evdev_for_xkb_keycode(event.nativeScanCode());
    return evdev ? "evdev:" + std::to_string(*evdev) : std::string("evdev:none");
}

}  // namespace

std::optional<HostKey> host_key_from_event(const QKeyEvent& event, HostModifiers held) {
    const auto evdev = evdev_for_xkb_keycode(event.nativeScanCode());
    if (!evdev) {
        return std::nullopt;
    }
    HostKey key;
    key.evdev = evdev;
    key.code = std::string(w3c_code_for_evdev(*evdev));
    key.modifiers = held;
    key.text = (held & MODIFIER_CONTROL)
                   ? character_ignoring_ctrl(event, (held & MODIFIER_SHIFT) != 0)
                   : event.text().toStdString();
    return key;
}

std::optional<MappingFile> built_in_mapping(const QString& name) {
    QFile file(QStringLiteral(":/keyboard-mappings/%1.json").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = file.readAll();
    auto result = parse_mapping_file(std::string_view(bytes.constData(),
                                                      static_cast<std::size_t>(bytes.size())));
    if (auto* mapping = std::get_if<MappingFile>(&result)) {
        return std::move(*mapping);
    }
    return std::nullopt;
}

KeyboardInput::KeyboardInput() {
    if (auto mapping = built_in_mapping(QStringLiteral("Logical"))) {
        mapping_ = std::move(*mapping);
    }
}

void KeyboardInput::set_mapping(MappingFile mapping) {
    release_all();
    mapping_ = std::move(mapping);
    // Switching synchronisation on is a sync point (7.3).
    sync_caps_lock();
}

bool KeyboardInput::press_named_key(std::string_view source, std::string_view name) {
    if (!send_) {
        return false;
    }
    ResolvedKey key;
    if (name == "Break") {
        key.role = KeyRole::break_key;
    } else {
        const auto ik = table_.ik_by_name(name);
        if (!ik) {
            return false;
        }
        key.ik_number = *ik;
        key.role = name == "Shift" ? KeyRole::shift : name == "Ctrl" ? KeyRole::ctrl : KeyRole::matrix;
    }
    const std::string id = std::string(source) + ":" + std::string(name);
    send_(held_.press(id, key));
    return true;
}

void KeyboardInput::release_named_key(std::string_view source, std::string_view name) {
    const auto events = held_.release(std::string(source) + ":" + std::string(name));
    if (send_ && !events.empty()) {
        send_(events);
    }
}

void KeyboardInput::set_caps_lock_sync_override(std::optional<bool> on) {
    caps_lock_sync_override_ = on;
    sync_caps_lock();
}

void KeyboardInput::sync_caps_lock() {
    if (!send_ || !caps_lock_sync_enabled() || !host_caps_lock_) {
        return;
    }
    if (const auto host_on = host_caps_lock_()) {
        WireEvent sync;
        sync.action = WireAction::caps_lock_sync;
        sync.caps_lock_on = *host_on;
        send_({sync});
    }
}

void KeyboardInput::attach(BbcKeyTable table, Sender send) {
    release_all();
    table_ = std::move(table);
    send_ = std::move(send);
    // Connecting is a sync point (7.3).
    sync_caps_lock();
}

void KeyboardInput::detach() {
    release_all();
    send_ = nullptr;
    table_ = BbcKeyTable();
}

HostModifiers KeyboardInput::held_modifiers() const {
    HostModifiers held = 0;
    for (const auto& [id, modifier] : host_modifiers_down_) {
        held |= modifier;
    }
    return held;
}

namespace {

constexpr HostModifiers NOT_THE_MACHINES = MODIFIER_ALT | MODIFIER_META;

bool bound_with_alt_or_meta(const MappingFile& mapping, const std::string& code) {
    return std::any_of(mapping.keys.begin(), mapping.keys.end(), [&](const MappingEntry& e) {
        return e.applies && e.code && *e.code == code && (e.with & NOT_THE_MACHINES);
    });
}

}  // namespace

bool KeyboardInput::window_chord(const QKeyEvent& event) const {
    const auto host_key = host_key_from_event(event, held_modifiers());
    return host_key && (host_key->modifiers & NOT_THE_MACHINES) &&
           modifier_for_code(host_key->code) == 0 &&
           !bound_with_alt_or_meta(mapping_, host_key->code);
}

bool KeyboardInput::takes(const QKeyEvent& event) const {
    if (!send_ || event.key() == Qt::Key_F11 || window_chord(event)) {
        return false;
    }
    const auto host_key = host_key_from_event(event, held_modifiers());
    if (!host_key) {
        return false;
    }
    if ((host_key->modifiers & NOT_THE_MACHINES) &&
        !bound_with_alt_or_meta(mapping_, host_key->code)) {
        return false;
    }
    if (host_key->code == "CapsLock" && caps_lock_sync_enabled()) {
        return true;
    }
    return resolve_key(mapping_, table_, *host_key).has_value();
}

bool KeyboardInput::key_press(const QKeyEvent& event) {
    if (event.isAutoRepeat()) {
        return true;  // the MOS runs its own repeat (R7)
    }
    const auto host_key = host_key_from_event(event, held_modifiers());
    if (!host_key) {
        return false;
    }
    // Tracked before resolving anything else, mapped or not: a later key's
    // `with` is decided by it.
    if (const HostModifiers modifier = modifier_for_code(host_key->code)) {
        host_modifiers_down_[key_id(event)] = modifier;
    }
    if (!send_ || window_chord(event)) {
        return false;
    }
    // A synchronised Caps Lock is a toggle with its own path, never a held
    // key (7.3); unsynchronised, a mapping may bind it like any other key.
    if (host_key->code == "CapsLock" && caps_lock_sync_enabled()) {
        if (user_pressed_) {
            user_pressed_();
        }
        if (caps_lock_pressed_) {
            caps_lock_pressed_();
        }
        return true;
    }
    const auto resolved = resolve_key(mapping_, table_, *host_key);
    if (!resolved) {
        return false;
    }
    if (user_pressed_) {
        user_pressed_();
    }
    send_(held_.press(key_id(event), *resolved));
    return true;
}

bool KeyboardInput::key_release(const QKeyEvent& event) {
    if (event.isAutoRepeat()) {
        return true;
    }
    host_modifiers_down_.erase(key_id(event));
    // By identity alone: what the key resolved to was decided at its press,
    // and a release of a key never pressed sends nothing (R8).
    const auto events = held_.release(key_id(event));
    if (send_) {
        send_(events);
    }
    return !events.empty();
}

void KeyboardInput::press_break() {
    if (!send_) {
        return;
    }
    ResolvedKey key;
    key.role = KeyRole::break_key;
    if (user_pressed_) {
        user_pressed_();
    }
    send_(held_.press(QStringLiteral("bbc:Break").toStdString(), key));
}

void KeyboardInput::release_break() {
    const auto events = held_.release("bbc:Break");
    if (send_) {
        send_(events);
    }
}

void KeyboardInput::yield_to_window() {
    const auto events = held_.release_all();
    if (send_) {
        send_(events);
    }
}

void KeyboardInput::release_all() {
    host_modifiers_down_.clear();
    const auto events = held_.release_all();
    if (send_) {
        send_(events);
    }
}

}  // namespace beebium::linux_client

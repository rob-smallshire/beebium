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

namespace beebium::linux_client {

namespace {

// The character a key gives without CTRL. Qt reports Ctrl+M as text "\r",
// but key() still names M; letters come back as capitals, so take the lower
// case, which is what the key types unshifted (R9).
std::string character_ignoring_ctrl(const QKeyEvent& event) {
    const int key = event.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return std::string(1, static_cast<char>('a' + (key - Qt::Key_A)));
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

std::optional<HostKey> host_key_from_event(const QKeyEvent& event) {
    const auto evdev = evdev_for_xkb_keycode(event.nativeScanCode());
    if (!evdev) {
        return std::nullopt;
    }
    const std::string_view code = w3c_code_for_evdev(*evdev);
    if (code.empty()) {
        return std::nullopt;
    }
    HostKey key;
    key.code = std::string(code);
    key.text = event.text().toStdString();
    key.ctrl_held = event.modifiers().testFlag(Qt::ControlModifier);
    key.alt_held = event.modifiers().testFlag(Qt::AltModifier);
    if (key.ctrl_held) {
        key.text_ignoring_ctrl = character_ignoring_ctrl(event);
    }
    return key;
}

void KeyboardInput::attach(BbcKeyTable table, Sender send) {
    release_all();
    table_ = std::move(table);
    send_ = std::move(send);
}

void KeyboardInput::detach() {
    release_all();
    send_ = nullptr;
    table_ = BbcKeyTable();
}

bool KeyboardInput::key_press(const QKeyEvent& event) {
    if (event.isAutoRepeat()) {
        return true;  // the MOS runs its own repeat (R7)
    }
    if (!send_) {
        return false;
    }
    const auto host_key = host_key_from_event(event);
    if (!host_key) {
        return false;
    }
    const auto resolved = resolve_key(mapping_, table_, *host_key);
    if (!resolved) {
        return false;
    }
    send_(held_.press(key_id(event), *resolved));
    return true;
}

bool KeyboardInput::key_release(const QKeyEvent& event) {
    if (event.isAutoRepeat()) {
        return true;
    }
    // By identity alone: what the key resolved to was decided at its press,
    // and a release of a key never pressed sends nothing (R8).
    const auto events = held_.release(key_id(event));
    if (send_) {
        send_(events);
    }
    return !events.empty();
}

void KeyboardInput::release_all() {
    const auto events = held_.release_all();
    if (send_) {
        send_(events);
    }
}

}  // namespace beebium::linux_client

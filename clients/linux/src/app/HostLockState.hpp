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

#ifndef BEEBIUM_LINUX_APP_HOST_LOCK_STATE_HPP
#define BEEBIUM_LINUX_APP_HOST_LOCK_STATE_HPP

#include <QObject>

#include <cstdint>
#include <memory>
#include <optional>

namespace beebium::linux_client {

// The host's Caps Lock state, read exactly from the windowing system, for
// synchronising the guest's (client specification 7.3).
//
// - X11: the core keyboard state's Lock bit (QueryPointer), at any time.
// - Wayland: the locked modifiers the compositor sends this client, through
//   a seat and keyboard of our own (created only once the seat has a
//   keyboard), decoded with the seat's XKB keymap.
//   The compositor sends them only while the client has keyboard focus, as
//   Wayland intends; the state is the one at the last focus or change.
// - Anything else (offscreen, eglfs): unknown, and synchronisation does
//   nothing.
class HostLockState : public QObject {
    Q_OBJECT

public:
    // The implementation for the platform QGuiApplication is running on.
    static std::unique_ptr<HostLockState> create(QObject* parent = nullptr);

    using QObject::QObject;

    virtual std::optional<bool> caps_lock() const = 0;

Q_SIGNALS:
    // Wayland only: the compositor reported a change.
    void caps_lock_changed(bool on);
};

// Whether Caps Lock is locked in an XKB locked-modifier mask, for the keymap
// whose Caps modifier has index `caps_index`. Nullopt if the keymap has none.
std::optional<bool> caps_locked_in_mask(std::uint32_t locked_mods,
                                        std::optional<std::uint32_t> caps_index);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_HOST_LOCK_STATE_HPP

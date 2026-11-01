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

#include "app/HostLockState.hpp"

#include <QGuiApplication>
#include <qguiapplication_platform.h>

#if defined(BEEBIUM_HAS_X11_LOCKS)
#include <xcb/xcb.h>
#endif

#if defined(BEEBIUM_HAS_WAYLAND_LOCKS)
#include <sys/mman.h>

#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#endif

namespace beebium::linux_client {

std::optional<bool> caps_locked_in_mask(std::uint32_t locked_mods,
                                        std::optional<std::uint32_t> caps_index) {
    if (!caps_index || *caps_index >= 32) {
        return std::nullopt;
    }
    return (locked_mods & (std::uint32_t{1} << *caps_index)) != 0;
}

namespace {

class UnknownLockState final : public HostLockState {
public:
    using HostLockState::HostLockState;
    std::optional<bool> caps_lock() const override { return std::nullopt; }
};

#if defined(BEEBIUM_HAS_X11_LOCKS)

class X11LockState final : public HostLockState {
public:
    X11LockState(xcb_connection_t* connection, QObject* parent)
        : HostLockState(parent), connection_(connection) {
        root_ = xcb_setup_roots_iterator(xcb_get_setup(connection_)).data->root;
    }

    std::optional<bool> caps_lock() const override {
        // The core keyboard state, Lock included, comes with the pointer
        // query: one round trip, no XKB extension needed.
        xcb_query_pointer_reply_t* reply =
            xcb_query_pointer_reply(connection_, xcb_query_pointer(connection_, root_), nullptr);
        if (!reply) {
            return std::nullopt;
        }
        const bool locked = (reply->mask & XCB_MOD_MASK_LOCK) != 0;
        free(reply);
        return locked;
    }

private:
    xcb_connection_t* connection_;
    xcb_window_t root_ = 0;
};

#endif

#if defined(BEEBIUM_HAS_WAYLAND_LOCKS)

class WaylandLockState final : public HostLockState {
public:
    explicit WaylandLockState(wl_display* display, QObject* parent)
        : HostLockState(parent), context_(xkb_context_new(XKB_CONTEXT_NO_FLAGS)) {
        // A seat of our own, bound through the registry, so this reader can
        // hear the seat's capabilities: asking a seat with no keyboard for one
        // is a protocol error that ends the connection, and a seat may gain or
        // lose its keyboard at any time. Qt keeps its own seat and listeners.
        registry_ = wl_display_get_registry(display);
        static const wl_registry_listener registry_listener = {
            &WaylandLockState::on_global, &WaylandLockState::on_global_remove};
        wl_registry_add_listener(registry_, &registry_listener, this);
    }

    ~WaylandLockState() override {
        release_keyboard();
        if (seat_) {
            if (wl_seat_get_version(seat_) >= WL_SEAT_RELEASE_SINCE_VERSION) {
                wl_seat_release(seat_);
            } else {
                wl_seat_destroy(seat_);
            }
        }
        if (registry_) {
            wl_registry_destroy(registry_);
        }
        if (keymap_) {
            xkb_keymap_unref(keymap_);
        }
        if (context_) {
            xkb_context_unref(context_);
        }
    }

    std::optional<bool> caps_lock() const override { return caps_; }

private:
    static void on_global(void* data, wl_registry* registry, uint32_t name,
                          const char* interface, uint32_t version) {
        auto* self = static_cast<WaylandLockState*>(data);
        if (self->seat_ || std::strcmp(interface, wl_seat_interface.name) != 0) {
            return;  // the first seat is the one Qt uses too
        }
        self->seat_ = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, std::min<uint32_t>(version, 5)));
        static const wl_seat_listener seat_listener = {&WaylandLockState::on_capabilities,
                                                       &WaylandLockState::on_seat_name};
        wl_seat_add_listener(self->seat_, &seat_listener, self);
    }

    static void on_global_remove(void*, wl_registry*, uint32_t) {}

    static void on_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
        auto* self = static_cast<WaylandLockState*>(data);
        const bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
        if (has_keyboard && !self->keyboard_) {
            self->keyboard_ = wl_seat_get_keyboard(seat);
            static const wl_keyboard_listener listener = {
                &WaylandLockState::on_keymap,    &WaylandLockState::on_enter,
                &WaylandLockState::on_leave,     &WaylandLockState::on_key,
                &WaylandLockState::on_modifiers, &WaylandLockState::on_repeat_info,
            };
            wl_keyboard_add_listener(self->keyboard_, &listener, self);
        } else if (!has_keyboard && self->keyboard_) {
            self->release_keyboard();
            self->caps_.reset();  // no keyboard, no state
        }
    }

    static void on_seat_name(void*, wl_seat*, const char*) {}

    void release_keyboard() {
        if (!keyboard_) {
            return;
        }
        if (wl_keyboard_get_version(keyboard_) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
            wl_keyboard_release(keyboard_);
        } else {
            wl_keyboard_destroy(keyboard_);
        }
        keyboard_ = nullptr;
    }

    static void on_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
        auto* self = static_cast<WaylandLockState*>(data);
        if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && self->context_) {
            void* text = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (text != MAP_FAILED) {
                xkb_keymap* keymap = xkb_keymap_new_from_buffer(
                    self->context_, static_cast<const char*>(text), size - 1,
                    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
                munmap(text, size);
                if (keymap) {
                    if (self->keymap_) {
                        xkb_keymap_unref(self->keymap_);
                    }
                    self->keymap_ = keymap;
                    const xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
                    self->caps_index_ = index == XKB_MOD_INVALID
                                            ? std::nullopt
                                            : std::optional<std::uint32_t>(index);
                }
            }
        }
        close(fd);
    }

    static void on_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t,
                             uint32_t locked, uint32_t) {
        auto* self = static_cast<WaylandLockState*>(data);
        const auto caps = caps_locked_in_mask(locked, self->caps_index_);
        if (caps && caps != self->caps_) {
            self->caps_ = caps;
            emit self->caps_lock_changed(*caps);
        }
    }

    static void on_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
    static void on_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
    static void on_key(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void on_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

    xkb_context* context_ = nullptr;
    xkb_keymap* keymap_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_seat* seat_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    std::optional<std::uint32_t> caps_index_;
    std::optional<bool> caps_;
};

#endif

}  // namespace

std::unique_ptr<HostLockState> HostLockState::create(QObject* parent) {
#if defined(BEEBIUM_HAS_X11_LOCKS)
    if (auto* x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        if (xcb_connection_t* connection = x11->connection()) {
            return std::make_unique<X11LockState>(connection, parent);
        }
    }
#endif
#if defined(BEEBIUM_HAS_WAYLAND_LOCKS)
    if (auto* wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        if (wl_display* display = wayland->display()) {
            return std::make_unique<WaylandLockState>(display, parent);
        }
    }
#endif
    return std::make_unique<UnknownLockState>(parent);
}

}  // namespace beebium::linux_client

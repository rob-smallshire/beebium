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

// Caps Lock synchronisation, client specification 7.3: the decision and the
// host state readers. The guest-ready sync point is the session's
// (test_machine_session.cpp).

#include "app/HostLockState.hpp"
#include "keyboard/CapsLockSync.hpp"

#include <catch2/catch_test_macros.hpp>

#include <xkbcommon/xkbcommon.h>


using namespace beebium::linux_client;

TEST_CASE("The guest is tapped only when sync is on and the states differ",
          "[caps-lock]") {
    CHECK(should_tap_caps_lock(true, true, false));
    CHECK(should_tap_caps_lock(true, false, true));
    CHECK_FALSE(should_tap_caps_lock(true, true, true));
    CHECK_FALSE(should_tap_caps_lock(true, false, false));
    CHECK_FALSE(should_tap_caps_lock(false, true, false));
}

TEST_CASE("An unknown state on either side taps nothing", "[caps-lock]") {
    CHECK_FALSE(should_tap_caps_lock(true, std::nullopt, false));
    CHECK_FALSE(should_tap_caps_lock(true, true, std::nullopt));
}

TEST_CASE("Caps Lock is read from the locked modifiers by the keymap's index",
          "[caps-lock][xkb]") {
    // A real keymap, as a compositor would send it.
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    REQUIRE(context);
    const xkb_rule_names names{"evdev", "pc105", "gb", "", ""};
    xkb_keymap* keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    REQUIRE(keymap);
    const xkb_mod_index_t caps = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    REQUIRE(caps != XKB_MOD_INVALID);
    const xkb_mod_index_t num = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_NUM);

    CHECK(caps_locked_in_mask(1u << caps, caps) == true);
    CHECK(caps_locked_in_mask(0, caps) == false);
    // Num Lock alone is not Caps Lock.
    CHECK(caps_locked_in_mask(1u << num, caps) == false);
    CHECK(caps_locked_in_mask((1u << num) | (1u << caps), caps) == true);

    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
}

TEST_CASE("A keymap without a Caps modifier gives no answer", "[caps-lock]") {
    CHECK_FALSE(caps_locked_in_mask(0xffffffffu, std::nullopt).has_value());
    CHECK_FALSE(caps_locked_in_mask(1, 40).has_value());
}

TEST_CASE("With no windowing system the host state is unknown", "[caps-lock]") {
    // The tests run under the offscreen platform.
    const auto locks = HostLockState::create();
    REQUIRE(locks);
    CHECK_FALSE(locks->caps_lock().has_value());
}

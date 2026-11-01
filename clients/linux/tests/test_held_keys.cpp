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

// The held-key set against frontend-modifier-keys.md and the scenarios of
// client specification 7.2.

#include "keyboard/HeldKeys.hpp"

#include "SampleKeyTable.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using beebium::linux_client::testing::sample_key_table;
using Events = std::vector<WireEvent>;

namespace {

constexpr WireEvent down(std::uint8_t ik) { return {WireAction::key_down, ik}; }
constexpr WireEvent up(std::uint8_t ik) { return {WireAction::key_up, ik}; }
constexpr std::uint8_t SHIFT = BBC_SHIFT_IK;
constexpr std::uint8_t CTRL = BBC_CTRL_IK;

ResolvedKey resolve(std::string code, std::string text = {}, bool ctrl = false,
                    std::string text_ignoring_ctrl = {}, bool alt = false) {
    HostKey host;
    host.code = std::move(code);
    host.text = std::move(text);
    host.ctrl_held = ctrl;
    host.text_ignoring_ctrl = std::move(text_ignoring_ctrl);
    host.alt_held = alt;
    const auto resolved = resolve_key(logical_key_mapping(), sample_key_table(), host);
    REQUIRE(resolved.has_value());
    return *resolved;
}

}  // namespace

TEST_CASE("A plain key is one down and one up", "[keyboard][held]") {
    HeldKeys held;
    CHECK(held.press("evdev:30", resolve("KeyA", "a")) == Events{down(0x41)});
    CHECK(held.release("evdev:30") == Events{up(0x41)});
    CHECK(held.empty());
}

TEST_CASE("A US apostrophe synthesises SHIFT around the 7 key", "[keyboard][held]") {
    HeldKeys held;
    CHECK(held.press("evdev:40", resolve("Quote", "'")) == Events{down(SHIFT), down(0x24)});
    CHECK(held.release("evdev:40") == Events{up(0x24), up(SHIFT)});
}

TEST_CASE("A US colon suppresses the user's SHIFT while it is held", "[keyboard][held]") {
    HeldKeys held;
    CHECK(held.press("evdev:42", resolve("ShiftLeft")) == Events{down(SHIFT)});
    // Modifiers move before the key goes down.
    CHECK(held.press("evdev:39", resolve("Semicolon", ":")) == Events{up(SHIFT), down(0x48)});
    // The key comes up before the modifiers move back.
    CHECK(held.release("evdev:39") == Events{up(0x48), down(SHIFT)});
    CHECK(held.release("evdev:42") == Events{up(SHIFT)});
}

TEST_CASE("A shifted character rides the user's SHIFT", "[keyboard][held]") {
    HeldKeys held;
    held.press("evdev:42", resolve("ShiftLeft"));
    // Physical SHIFT already gives the shifted face: nothing to add.
    CHECK(held.press("evdev:30", resolve("KeyA", "A")) == Events{down(0x41)});
    // Facts were frozen at the press: releasing SHIFT releases BBC SHIFT.
    CHECK(held.release("evdev:42") == Events{up(SHIFT)});
    CHECK(held.release("evdev:30") == Events{up(0x41)});
}

TEST_CASE("Holding both SHIFTs and releasing one leaves SHIFT down, not stuck",
          "[keyboard][held]") {
    HeldKeys held;
    CHECK(held.press("evdev:42", resolve("ShiftLeft")) == Events{down(SHIFT)});
    CHECK(held.press("evdev:54", resolve("ShiftRight")).empty());
    CHECK(held.release("evdev:42").empty());
    CHECK(held.bbc_shift_down());
    CHECK(held.release("evdev:54") == Events{up(SHIFT)});
    CHECK_FALSE(held.bbc_shift_down());
}

TEST_CASE("CTRL with a letter is CTRL and that letter, not RETURN", "[keyboard][held]") {
    HeldKeys held;
    CHECK(held.press("evdev:29", resolve("ControlLeft")) == Events{down(CTRL)});
    CHECK(held.press("evdev:50", resolve("KeyM", "\r", true, "m")) == Events{down(0x65)});
    CHECK(held.release("evdev:50") == Events{up(0x65)});
    CHECK(held.release("evdev:29") == Events{up(CTRL)});
}

TEST_CASE("Releasing everything leaves nothing down", "[keyboard][held]") {
    // Hold SHIFT, switch away: everything comes up, keys before modifiers.
    HeldKeys held;
    held.press("evdev:42", resolve("ShiftLeft"));
    held.press("evdev:30", resolve("KeyA", "A"));
    CHECK(held.release_all() == Events{up(0x41), up(SHIFT)});
    CHECK(held.empty());
    CHECK_FALSE(held.bbc_shift_down());
    // Back again: the late key-ups find nothing held and send nothing.
    CHECK(held.release("evdev:30").empty());
    CHECK(held.release("evdev:42").empty());
    // And typing is lowercase.
    CHECK(held.press("evdev:30", resolve("KeyA", "a")) == Events{down(0x41)});
}

TEST_CASE("A release of a key never pressed sends nothing", "[keyboard][held]") {
    HeldKeys held;
    CHECK(held.release("evdev:30").empty());
}

TEST_CASE("A second press of a held key sends nothing", "[keyboard][held]") {
    HeldKeys held;
    held.press("evdev:30", resolve("KeyA", "a"));
    CHECK(held.press("evdev:30", resolve("KeyA", "a")).empty());
}

TEST_CASE("Break goes to the reset line, not the matrix", "[keyboard][held]") {
    HeldKeys held;
    const auto alt_f12 = resolve("F12", {}, false, {}, true);
    CHECK(held.press("evdev:88", alt_f12) == Events{{WireAction::break_down, 0}});
    CHECK(held.release("evdev:88") == Events{{WireAction::break_up, 0}});

    held.press("evdev:88", alt_f12);
    CHECK(held.release_all() == Events{{WireAction::break_up, 0}});
}

TEST_CASE("A direct entry's explicit SHIFT is synthesised", "[keyboard][held]") {
    ResolvedKey key;
    key.ik_number = 0x41;
    key.bbc_shift = true;
    HeldKeys held;
    CHECK(held.press("evdev:44", key) == Events{down(SHIFT), down(0x41)});
    CHECK(held.release("evdev:44") == Events{up(0x41), up(SHIFT)});
}

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

#include "keyboard/KeyResolver.hpp"

#include "SampleKeyTable.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using beebium::linux_client::testing::sample_key_table;

namespace {

HostKey key(std::string code, std::string text = {}) {
    HostKey host;
    host.code = std::move(code);
    host.text = std::move(text);
    return host;
}

}  // namespace

TEST_CASE("Logical maps the cursor and function keys directly", "[keyboard][resolve]") {
    const auto mapping = logical_key_mapping();
    const auto table = sample_key_table();

    const auto up = resolve_key(mapping, table, key("ArrowUp"));
    REQUIRE(up);
    CHECK(up->role == KeyRole::matrix);
    CHECK(up->ik_number == 0x39);
    CHECK_FALSE(up->from_character_mapping);

    CHECK(resolve_key(mapping, table, key("F10"))->ik_number == 0x20);  // f0
    CHECK(resolve_key(mapping, table, key("F11"))->ik_number == 0x69);  // Copy
    CHECK(resolve_key(mapping, table, key("Backspace"))->ik_number == 0x59);  // Delete
}

TEST_CASE("The host's SHIFT and CTRL keys resolve to the modifier roles",
          "[keyboard][resolve]") {
    const auto mapping = logical_key_mapping();
    const auto table = sample_key_table();
    CHECK(resolve_key(mapping, table, key("ShiftLeft"))->role == KeyRole::shift);
    CHECK(resolve_key(mapping, table, key("ShiftRight"))->role == KeyRole::shift);
    CHECK(resolve_key(mapping, table, key("ControlLeft"))->role == KeyRole::ctrl);
    CHECK(resolve_key(mapping, table, key("ControlRight"))->role == KeyRole::ctrl);
}

TEST_CASE("Break needs Alt+F12, and F12 alone is not Break", "[keyboard][resolve]") {
    const auto mapping = logical_key_mapping();
    const auto table = sample_key_table();
    auto alt_f12 = key("F12");
    alt_f12.alt_held = true;
    const auto resolved = resolve_key(mapping, table, alt_f12);
    REQUIRE(resolved);
    CHECK(resolved->role == KeyRole::break_key);
    CHECK_FALSE(resolve_key(mapping, table, key("F12")).has_value());
}

TEST_CASE("Character keys resolve through the server's table", "[keyboard][resolve]") {
    const auto mapping = logical_key_mapping();
    const auto table = sample_key_table();

    // On a US layout ' is unshifted on the host, but the shifted face of 7.
    const auto apostrophe = resolve_key(mapping, table, key("Quote", "'"));
    REQUIRE(apostrophe);
    CHECK(apostrophe->from_character_mapping);
    CHECK(apostrophe->ik_number == 0x24);
    CHECK(apostrophe->bbc_shift);

    // Shift+; gives : on a US host, the BBC's unshifted : key.
    const auto colon = resolve_key(mapping, table, key("Semicolon", ":"));
    REQUIRE(colon);
    CHECK(colon->ik_number == 0x48);
    CHECK_FALSE(colon->bbc_shift);

    const auto enter = resolve_key(mapping, table, key("Enter", "\r"));
    REQUIRE(enter);
    CHECK(enter->ik_number == 0x49);
}

TEST_CASE("Under CTRL the key is resolved from its unmodified character",
          "[keyboard][resolve]") {
    // The host reports Ctrl+M as a carriage return; the machine must see M.
    const auto mapping = logical_key_mapping();
    const auto table = sample_key_table();
    auto ctrl_m = key("KeyM", "\r");
    ctrl_m.ctrl_held = true;
    ctrl_m.text_ignoring_ctrl = "m";
    const auto resolved = resolve_key(mapping, table, ctrl_m);
    REQUIRE(resolved);
    CHECK(resolved->ik_number == 0x65);
    CHECK_FALSE(resolved->bbc_ctrl);
}

TEST_CASE("A key with no entry and no known character maps to nothing",
          "[keyboard][resolve]") {
    const auto mapping = logical_key_mapping();
    const auto table = sample_key_table();
    CHECK_FALSE(resolve_key(mapping, table, key("MetaLeft")).has_value());
    CHECK_FALSE(resolve_key(mapping, table, key("KeyQ", "\xc3\xa9")).has_value());
}

TEST_CASE("Without character mapping only direct entries resolve", "[keyboard][resolve]") {
    auto mapping = logical_key_mapping();
    mapping.character_mapping = false;
    const auto table = sample_key_table();
    CHECK(resolve_key(mapping, table, key("ArrowUp")).has_value());
    CHECK_FALSE(resolve_key(mapping, table, key("KeyA", "a")).has_value());
}

TEST_CASE("A direct entry carries its explicit SHIFT and CTRL", "[keyboard][resolve]") {
    KeyMapping mapping;
    mapping.character_mapping = false;
    mapping.entries = {{"KeyZ", false, "a", true, true}};
    const auto resolved = resolve_key(mapping, sample_key_table(), key("KeyZ"));
    REQUIRE(resolved);
    CHECK(resolved->ik_number == 0x41);
    CHECK(resolved->bbc_shift);
    CHECK(resolved->bbc_ctrl);
}

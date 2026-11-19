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

// Resolution details the shared corpus leaves to each client: this client's
// own platform entries, and keys the connected machine does not have.

#include "keyboard/KeyResolver.hpp"

#include "Corpus.hpp"
#include "SampleKeyTable.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

MappingFile mapping_of(const std::string& keys, bool character_mapping = true) {
    auto result = parse_mapping_file(R"({"version": 2, "id": "x", "name": "n", "characterMapping": )" +
                                     std::string(character_mapping ? "true" : "false") +
                                     R"(, "keys": [)" + keys + "]}");
    REQUIRE(std::holds_alternative<MappingFile>(result));
    return std::get<MappingFile>(std::move(result));
}

}  // namespace

TEST_CASE("A linux platform entry matches the event's evdev code", "[keyboard][resolve]") {
    const auto mapping =
        mapping_of(R"({"platform": "linux", "scancode": 240, "bbc": {"key": "f0"}})");
    HostKey key;
    key.evdev = 240;  // KEY_UNKNOWN: no W3C code
    const auto resolved = resolve_key(mapping, sample_key_table(), key);
    REQUIRE(resolved);
    CHECK(resolved->ik_number == 0x20);

    key.evdev = 241;
    CHECK_FALSE(resolve_key(mapping, sample_key_table(), key).has_value());
}

TEST_CASE("Another platform's entry never matches here", "[keyboard][resolve]") {
    const auto mapping =
        mapping_of(R"({"platform": "macOS", "keyCode": 240, "bbc": {"key": "f0"}})", false);
    HostKey key;
    key.evdev = 240;
    CHECK_FALSE(resolve_key(mapping, sample_key_table(), key).has_value());
}

namespace {

// A Model B's catalogue with a Master's keypad 7 in it as well: the key
// number is this test's own.
BbcKeyTable with_keypad_7() {
    return BbcKeyTable({
        {"", 0x00, false, "Shift"},
        {"", 0x01, false, "Ctrl"},
        {"7", 0x24, false, "7"},
        {"'", 0x24, true, "'"},
        {"", 0x1b, false, "Keypad7"},
    });
}

}  // namespace

TEST_CASE("A keypad entry is the Keypad key on a machine that has it, and types 7 on one that "
          "does not",
          "[keyboard][resolve][machine]") {
    // docs/keyboard-mapping-files.md 3.6: an entry whose BBC key the
    // connected machine's catalogue does not list does not apply.
    const auto mapping = mapping_of(R"({"code": "Numpad7", "bbc": {"key": "Keypad7"}})");
    HostKey key;
    key.code = "Numpad7";
    key.text = "7";
    // A Model B: no keypad, so the host key falls through to its character.
    const auto model_b = resolve_key(mapping, sample_key_table(), key);
    REQUIRE(model_b);
    CHECK(model_b->ik_number == 0x24);
    CHECK(model_b->from_character_mapping);
    CHECK_FALSE(model_b->bbc_shift);
    // A machine with the keypad: the entry wins.
    const auto master = resolve_key(mapping, with_keypad_7(), key);
    REQUIRE(master);
    CHECK(master->ik_number == 0x1b);
    CHECK_FALSE(master->from_character_mapping);
}

TEST_CASE("An entry for a key the machine lacks gives way to the next rule, never to nothing",
          "[keyboard][resolve][machine]") {
    HostKey key;
    key.code = "KeyA";
    key.text = "a";
    // A misspelling behaves the same way on every machine.
    const auto misspelt = mapping_of(R"({"code": "KeyA", "bbc": {"key": "NoSuchKey"}})");
    const auto through = resolve_key(misspelt, sample_key_table(), key);
    REQUIRE(through);
    CHECK(through->ik_number == 0x41);
    CHECK(through->from_character_mapping);
    // A less exact entry for a key the machine has wins over it.
    key.modifiers = MODIFIER_ALT;
    const auto fallback = mapping_of(R"({"code": "KeyA", "with": ["Alt"], "bbc": {"key": "NoSuchKey"}},
        {"code": "KeyA", "bbc": {"key": "f0"}})");
    const auto plain = resolve_key(fallback, sample_key_table(), key);
    REQUIRE(plain);
    CHECK(plain->ik_number == 0x20);
    // Without character mapping, nothing is left: the key is ignored.
    key.modifiers = 0;
    CHECK_FALSE(resolve_key(mapping_of(R"({"code": "KeyA", "bbc": {"key": "NoSuchKey"}})", false),
                            sample_key_table(), key)
                    .has_value());
    // A switched-off entry for such a key does not silence the host key.
    const auto off = mapping_of(R"({"code": "KeyA", "bbc": {"key": "NoSuchKey", "disabled": true}})");
    const auto typed = resolve_key(off, sample_key_table(), key);
    REQUIRE(typed);
    CHECK(typed->role == KeyRole::matrix);
    CHECK(typed->ik_number == 0x41);
    // Break is no catalogue key, and every machine has it.
    const auto brk = resolve_key(mapping_of(R"({"code": "F12", "bbc": {"key": "Break"}})"),
                                 sample_key_table(), [] {
                                     HostKey f12;
                                     f12.code = "F12";
                                     return f12;
                                 }());
    REQUIRE(brk);
    CHECK(brk->role == KeyRole::break_key);
}

TEST_CASE("A key the machine lacks is not offered to be switched off", "[keyboard][resolve][machine]") {
    const auto mapping = mapping_of(R"({"code": "Numpad7", "bbc": {"key": "Keypad7", "disabled": false}},
        {"code": "KeyA", "bbc": {"key": "A", "disabled": false}})");
    CHECK(switchable_keys(mapping, sample_key_table()) ==
          std::vector<std::pair<std::string, bool>>{{"A", false}});
    CHECK(switchable_keys(mapping, with_keypad_7()) ==
          std::vector<std::pair<std::string, bool>>{{"Keypad7", false}});
    // With no machine connected, nothing is judged against one.
    CHECK(switchable_keys(mapping).size() == 2);
}

TEST_CASE("The host SHIFT and CTRL keys resolve to the modifier roles under Logical",
          "[keyboard][resolve]") {
    const auto mapping = corpus_mapping("Logical.json");
    for (const char* code : {"ShiftLeft", "ShiftRight"}) {
        HostKey key;
        key.code = code;
        CHECK(resolve_key(mapping, sample_key_table(), key)->role == KeyRole::shift);
    }
    for (const char* code : {"ControlLeft", "ControlRight"}) {
        HostKey key;
        key.code = code;
        CHECK(resolve_key(mapping, sample_key_table(), key)->role == KeyRole::ctrl);
    }
}

TEST_CASE("The user's switches decide a switchable key, which never falls through",
          "[keyboard][resolve][switches]") {
    const auto mapping = mapping_of(R"({"code": "KeyA", "bbc": {"key": "A", "disabled": false}},
        {"code": "KeyH", "bbc": {"key": "H", "disabled": true}}, {"code": "KeyM", "bbc": {"key": "M"}})");
    CHECK(switchable_keys(mapping) ==
          std::vector<std::pair<std::string, bool>>{{"A", false}, {"H", true}});
    HostKey a;
    a.code = "KeyA";
    a.text = "a";  // the character path would also give A
    REQUIRE(resolve_key(mapping, sample_key_table(), a));
    CHECK(resolve_key(mapping, sample_key_table(), a)->role == KeyRole::matrix);
    const auto off = resolve_key(mapping, sample_key_table(), a, {{"A", true}});
    REQUIRE(off);
    CHECK(off->role == KeyRole::switched_off);
    // Off by the file, on by the user.
    HostKey h;
    h.code = "KeyH";
    REQUIRE(resolve_key(mapping, sample_key_table(), h));
    CHECK(resolve_key(mapping, sample_key_table(), h)->role == KeyRole::switched_off);
    const auto on = resolve_key(mapping, sample_key_table(), h, {{"H", false}});
    REQUIRE(on);
    CHECK(on->role == KeyRole::matrix);
}

TEST_CASE("Logical lets Escape and Break be switched off, both on to start", "[keyboard][resolve][switches]") {
    const auto logical = corpus_mapping("Logical.json");
    CHECK(switchable_keys(logical) ==
          std::vector<std::pair<std::string, bool>>{{"Break", false}, {"Escape", false}});
}

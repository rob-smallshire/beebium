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
// own platform entries, and names the catalogue does not know.

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

TEST_CASE("An entry naming a key the catalogue lacks resolves to nothing",
          "[keyboard][resolve]") {
    const auto mapping = mapping_of(R"({"code": "KeyA", "bbc": {"key": "NoSuchKey"}})", false);
    HostKey key;
    key.code = "KeyA";
    CHECK_FALSE(resolve_key(mapping, sample_key_table(), key).has_value());
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

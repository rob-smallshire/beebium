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

// Reading and writing mapping files (docs/keyboard-mapping-files.md 2, 3, 6).

#include "keyboard/MappingFile.hpp"

#include "Corpus.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

MappingFile parse_ok(const std::string& text) {
    auto result = parse_mapping_file(text);
    if (auto* error = std::get_if<MappingFileError>(&result)) {
        FAIL(error->reason);
    }
    return std::get<MappingFile>(std::move(result));
}

std::string parse_error(const std::string& text) {
    auto result = parse_mapping_file(text);
    REQUIRE(std::holds_alternative<MappingFileError>(result));
    return std::get<MappingFileError>(result).reason;
}

std::string with_keys(const std::string& keys) {
    return R"({"version": 2, "id": "x", "name": "n", "keys": [)" + keys + "]}";
}

}  // namespace

TEST_CASE("A mapping's fields and defaults are read", "[keyboard][mapping-file]") {
    const auto mapping = parse_ok(with_keys(
        R"({"code": "Digit8", "with": ["Shift", "Alt"],
            "bbc": {"key": ":", "shift": true, "ctrl": true, "disabled": true, "action": "Fire"}})"));
    CHECK(mapping.id == "x");
    CHECK(mapping.name == "n");
    CHECK(mapping.character_mapping);
    CHECK(mapping.synchronize_caps_lock);
    REQUIRE(mapping.keys.size() == 1);
    const auto& entry = mapping.keys[0];
    CHECK(entry.code == "Digit8");
    CHECK(entry.with == (MODIFIER_SHIFT | MODIFIER_ALT));
    CHECK(entry.bbc_key == ":");
    CHECK(entry.bbc_shift);
    CHECK(entry.bbc_ctrl);
    CHECK(entry.disabled == true);
    CHECK(entry.action == "Fire");
}

TEST_CASE("A linux platform entry is read by its evdev code", "[keyboard][mapping-file]") {
    const auto mapping =
        parse_ok(with_keys(R"({"platform": "linux", "scancode": 240, "bbc": {"key": "f0"}})"));
    REQUIRE(mapping.keys.size() == 1);
    CHECK(mapping.keys[0].platform == "linux");
    CHECK(mapping.keys[0].linux_scancode == 240u);
    CHECK_FALSE(mapping.keys[0].code.has_value());
}

TEST_CASE("Version 1, a newer version, or no version is refused", "[keyboard][mapping-file]") {
    CHECK(parse_error(R"({"version": 1, "id": "x", "name": "n", "keyMappings": []})")
              .find("version 1") != std::string::npos);
    CHECK(parse_error(R"({"version": 3, "id": "x", "name": "n", "keys": []})")
              .find("newer") != std::string::npos);
    parse_error(R"({"id": "x", "name": "n", "keys": []})");
    parse_error("not json");
}

TEST_CASE("Malformed entries are refused", "[keyboard][mapping-file]") {
    // Both a code and a platform.
    parse_error(with_keys(R"({"code": "KeyA", "platform": "linux", "scancode": 30,
                              "bbc": {"key": "A"}})"));
    // Neither.
    parse_error(with_keys(R"({"bbc": {"key": "A"}})"));
    // A linux platform entry for a key that has a code (evdev 30 is KeyA).
    parse_error(with_keys(R"({"platform": "linux", "scancode": 30, "bbc": {"key": "A"}})"));
    // An unknown host modifier.
    parse_error(with_keys(R"({"code": "KeyA", "with": ["Hyper"], "bbc": {"key": "A"}})"));
    // No BBC key.
    parse_error(with_keys(R"({"code": "KeyA", "bbc": {}})"));
}

TEST_CASE("Another platform's entries are kept, not applied", "[keyboard][mapping-file]") {
    const auto mapping = parse_ok(with_keys(
        R"({"platform": "macOS", "keyCode": 179, "bbc": {"key": "f1"}},
           {"platform": "amiga", "rawKey": 95, "bbc": {"key": "f3"}})"));
    REQUIRE(mapping.keys.size() == 2);
    CHECK(mapping.keys[0].platform == "macOS");
    CHECK_FALSE(mapping.keys[0].linux_scancode.has_value());
}

TEST_CASE("Writing preserves what this client does not understand",
          "[keyboard][mapping-file][round-trip]") {
    const Json original = Json::parse(corpus_text("rules.json"))["mappings"]["rules"];
    const MappingFile mapping = parse_ok(original.dump());
    const Json written = Json::parse(write_mapping_file(mapping));

    CHECK(written["x-unknown-top-level-field"] == original["x-unknown-top-level-field"]);
    REQUIRE(written["keys"].size() == original["keys"].size());
    for (std::size_t i = 0; i < original["keys"].size(); ++i) {
        INFO("entry " << i);
        CHECK(written["keys"][i] == original["keys"][i]);
    }
}

TEST_CASE("Every corpus mapping round-trips unchanged in meaning",
          "[keyboard][mapping-file][round-trip]") {
    for (const char* filename : {"Logical.json", "Arcadians.json", "Boffin.json",
                                 "Planetoid.json", "Snapper.json", "Thrust.json"}) {
        INFO(filename);
        const Json original = Json::parse(corpus_text(filename));
        const Json written = Json::parse(write_mapping_file(corpus_mapping(filename)));
        CHECK(written == original);
    }
}

TEST_CASE("Edits are written, with the rest kept", "[keyboard][mapping-file][round-trip]") {
    MappingFile mapping = parse_ok(with_keys(
        R"({"code": "KeyA", "x-note": "kept", "bbc": {"key": "A", "x-colour": "red"}})"));
    mapping.name = "renamed";
    mapping.keys[0].with = MODIFIER_CONTROL;
    mapping.keys[0].bbc_shift = true;
    const Json written = Json::parse(write_mapping_file(mapping));
    CHECK(written["name"] == "renamed");
    CHECK(written["keys"][0]["with"] == Json::array({"Control"}));
    CHECK(written["keys"][0]["bbc"]["shift"] == true);
    CHECK(written["keys"][0]["x-note"] == "kept");
    CHECK(written["keys"][0]["bbc"]["x-colour"] == "red");
}

TEST_CASE("A host modifier key names its modifier", "[keyboard][mapping-file]") {
    CHECK(modifier_for_code("ShiftRight") == MODIFIER_SHIFT);
    CHECK(modifier_for_code("ControlLeft") == MODIFIER_CONTROL);
    CHECK(modifier_for_code("AltRight") == MODIFIER_ALT);
    CHECK(modifier_for_code("MetaLeft") == MODIFIER_META);
    CHECK(modifier_for_code("KeyA") == 0);
}

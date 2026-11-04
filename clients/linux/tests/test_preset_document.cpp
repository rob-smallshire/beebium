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

// A preset as a document (client specification 5.2, 13.3): lossless, its
// unknown keys kept wherever they are, null the same as absent, and its own
// bytes written back when nothing was changed.

#include "preset/PresetDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

std::string read_text(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

PresetDocument parsed(std::string_view text) {
    auto result = PresetDocument::parse(text);
    REQUIRE(std::holds_alternative<PresetDocument>(result));
    return std::get<PresetDocument>(std::move(result));
}

}  // namespace

TEST_CASE("Every preset in the repository dumps its own bytes, and round-trips",
          "[preset][document]") {
    int checked = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(BEEBIUM_PRESET_FIXTURES_DIRPATH)) {
        if (entry.path().extension() != ".beebium") {
            continue;
        }
        INFO(entry.path().filename().string());
        const std::string text = read_text(entry.path());
        const PresetDocument document = parsed(text);
        CHECK_FALSE(document.modified());
        CHECK(document.dump() == text);
        // Decode, encode, decode: the same document.
        const PresetDocument again = parsed(document.json().dump(2));
        CHECK(again.json() == document.json());
        ++checked;
    }
    CHECK(checked >= 9);
}

TEST_CASE("Unknown keys survive an edit, at every level, in their order", "[preset][document]") {
    PresetDocument document = parsed(read_text(
        std::filesystem::path(BEEBIUM_PRESET_FIXTURES_DIRPATH) / "unknown_keys.preset.beebium"));
    document.set("/name", "Renamed");
    CHECK(document.modified());
    const ordered_json& json = document.json();
    CHECK(json["future_feature"]["some"] == "data");
    CHECK(json["storage"]["unknown_storage_key"] == true);
    std::vector<std::string> keys;
    for (const auto& [key, value] : json.items()) {
        keys.push_back(key);
    }
    CHECK(keys == std::vector<std::string>{"name", "model", "future_feature", "storage"});
    // Written as the server writes a preset.
    CHECK(document.dump() == json.dump(2) + "\n");
}

TEST_CASE("Null and absent both mean unset", "[preset][document]") {
    PresetDocument document = parsed(R"({"model": "model-b", "speed": null, "econet": null})");
    CHECK_FALSE(document.get("/speed").has_value());
    CHECK_FALSE(document.get("/description").has_value());
    CHECK(document.get("/model") == ordered_json("model-b"));
    document.set("/speed", 2.0);
    CHECK(document.get("/speed") == ordered_json(2.0));
    // Setting null unsets: the key goes.
    document.set("/speed", nullptr);
    CHECK_FALSE(document.json().contains("speed"));
    document.unset("/model");
    CHECK_FALSE(document.json().contains("model"));
    // Equivalence ignores the difference.
    CHECK(equivalent(parsed(R"({"a": 1, "b": null})").json(), parsed(R"({"a": 1})").json()));
    CHECK_FALSE(equivalent(parsed(R"({"a": 1})").json(), parsed(R"({"a": 2})").json()));
}

TEST_CASE("Integers stay integers; nested paths are made as they are set", "[preset][document]") {
    PresetDocument document = parsed(R"({"extensions": [{"name": "scsi-hdd", "config": {"scsi-id": 0}}]})");
    CHECK(document.json().dump() == R"({"extensions":[{"name":"scsi-hdd","config":{"scsi-id":0}}]})");
    document.set("/econet/station", 254);
    CHECK(document.json()["econet"]["station"].is_number_integer());
    document.set("/extensions/0/config/image", "/discs/0.dat");
    CHECK(document.get("/extensions/0/config/image") == ordered_json("/discs/0.dat"));
}

TEST_CASE("Only a JSON object is a preset", "[preset][document]") {
    CHECK(std::holds_alternative<std::string>(PresetDocument::parse("[1, 2]")));
    CHECK(std::holds_alternative<std::string>(PresetDocument::parse("{ not json")));
    CHECK(std::get<std::string>(PresetDocument::parse("[1]")) == "A preset must be a JSON object.");
}

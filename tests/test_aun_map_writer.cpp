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

// Unit tests for the aun-map.json writer: edits preserve entry order and
// unknown keys, emit the documented stable key order, and save atomically.

#include <catch2/catch_test_macros.hpp>

#include "AunMapFile.hpp"
#include "AunMapWriter.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace beebium;

namespace {
std::filesystem::path temp_filepath() {
    std::random_device rd;
    return std::filesystem::temp_directory_path() /
           ("beebium-mapw-" + std::to_string(rd()) + ".json");
}
void write_file(const std::filesystem::path& p, const std::string& text) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
}  // namespace

TEST_CASE("AunMapDocument: add a peer to an empty document", "[aun][map-writer]") {
    auto doc = AunMapDocument::empty();
    CHECK(doc.add_or_replace_peer(0, 254, "192.168.1.10", 32768, "fs").empty());
    // Round-trips through the parser.
    auto parsed = parse_aun_map(doc.to_json(), "test");
    REQUIRE(parsed.map.has_value());
    REQUIRE(parsed.map->peers.size() == 1);
    CHECK(parsed.map->peers[0].stn == 254);
    CHECK(parsed.map->peers[0].host == "192.168.1.10");
    CHECK(parsed.map->peers[0].label == "fs");
}

TEST_CASE("AunMapDocument: emits the documented stable key order",
          "[aun][map-writer]") {
    auto doc = AunMapDocument::empty();
    doc.add_or_replace_peer(0, 254, "192.168.1.10", 32768, "fs");
    doc.add_or_replace_subnet(128, "192.168.5.0/24", "risc os");
    const std::string json = doc.to_json();
    // net before station before host before port before label.
    CHECK(json.find("\"net\"") < json.find("\"station\""));
    CHECK(json.find("\"station\"") < json.find("\"host\""));
    CHECK(json.find("\"host\"") < json.find("\"port\""));
    CHECK(json.find("\"port\"") < json.find("\"label\""));
    // Pretty-printed with a trailing newline.
    CHECK(json.back() == '\n');
    CHECK(json.find("\n  ") != std::string::npos);
}

TEST_CASE("AunMapDocument: replacing a peer preserves order and unknown keys",
          "[aun][map-writer]") {
    // A hand-maintained file with two peers, an unknown per-entry key, and an
    // unknown top-level key.
    auto filepath = temp_filepath();
    write_file(filepath, R"({
      "schema": 7,
      "peers": [
        {"net": 0, "station": 1, "host": "10.0.0.1", "port": 32768, "note": "keep me"},
        {"net": 0, "station": 2, "host": "10.0.0.2", "port": 32768}
      ]
    })");

    auto load = AunMapDocument::load(filepath);
    REQUIRE(load.document.has_value());
    auto doc = *load.document;

    // Replace the first peer's endpoint.
    CHECK(doc.add_or_replace_peer(0, 1, "10.0.0.9", 40000, "").empty());
    const std::string json = doc.to_json();

    // Order preserved: station 1 still before station 2.
    auto pos1 = json.find("\"station\": 1");
    auto pos2 = json.find("\"station\": 2");
    REQUIRE(pos1 != std::string::npos);
    REQUIRE(pos2 != std::string::npos);
    CHECK(pos1 < pos2);
    // Unknown keys survive: the per-entry "note" and the top-level "schema".
    CHECK(json.find("\"note\"") != std::string::npos);
    CHECK(json.find("keep me") != std::string::npos);
    CHECK(json.find("\"schema\"") != std::string::npos);
    // The edit took effect.
    CHECK(json.find("10.0.0.9") != std::string::npos);
    CHECK(json.find("40000") != std::string::npos);

    std::filesystem::remove(filepath);
}

TEST_CASE("AunMapDocument: remove peer and subnet", "[aun][map-writer]") {
    auto doc = AunMapDocument::empty();
    doc.add_or_replace_peer(0, 1, "10.0.0.1", 32768, "");
    doc.add_or_replace_peer(0, 2, "10.0.0.2", 32768, "");
    doc.add_or_replace_subnet(128, "192.168.5.0/24", "");

    CHECK(doc.remove_peer(0, 1));
    CHECK_FALSE(doc.remove_peer(0, 99));  // not present
    CHECK(doc.remove_subnet(128));
    CHECK_FALSE(doc.remove_subnet(5));

    auto parsed = parse_aun_map(doc.to_json(), "test");
    REQUIRE(parsed.map.has_value());
    REQUIRE(parsed.map->peers.size() == 1);
    CHECK(parsed.map->peers[0].stn == 2);
    CHECK(parsed.map->subnets.empty());
}

TEST_CASE("AunMapDocument: validation errors name the field", "[aun][map-writer]") {
    auto doc = AunMapDocument::empty();
    CHECK(doc.add_or_replace_peer(0, 0, "10.0.0.1", 32768, "").find("station") !=
          std::string::npos);
    CHECK(doc.add_or_replace_peer(0, 1, "", 32768, "").find("host") !=
          std::string::npos);
    CHECK(doc.add_or_replace_subnet(1, "192.168.1.0/16", "").find("subnet") !=
          std::string::npos);
}

TEST_CASE("AunMapDocument: save writes atomically and round-trips",
          "[aun][map-writer]") {
    auto filepath = temp_filepath();
    auto doc = AunMapDocument::empty();
    doc.add_or_replace_peer(0, 254, "192.168.1.10", 32768, "fs");
    REQUIRE(doc.save(filepath).empty());

    // No leftover temporary files beside it.
    int temps = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(filepath.parent_path())) {
        if (entry.path().filename().string().rfind(
                "." + filepath.filename().string() + ".tmp.", 0) == 0) {
            ++temps;
        }
    }
    CHECK(temps == 0);

    auto reloaded = AunMapDocument::load(filepath);
    REQUIRE(reloaded.document.has_value());
    auto parsed = parse_aun_map(read_file(filepath), filepath.string());
    REQUIRE(parsed.map.has_value());
    REQUIRE(parsed.map->peers.size() == 1);
    CHECK(parsed.map->peers[0].host == "192.168.1.10");

    std::filesystem::remove(filepath);
}

TEST_CASE("AunMapDocument: an absent file loads as an empty editable document",
          "[aun][map-writer]") {
    auto load = AunMapDocument::load(temp_filepath());
    CHECK_FALSE(load.file_present);
    CHECK(load.error.empty());
    REQUIRE(load.document.has_value());
    // A first edit then creates it on save.
    CHECK(load.document->add_or_replace_peer(0, 1, "10.0.0.1", 32768, "").empty());
}

TEST_CASE("AunMapDocument: a malformed file is a load error with a position",
          "[aun][map-writer]") {
    auto filepath = temp_filepath();
    write_file(filepath, R"({"peers": [}})");
    auto load = AunMapDocument::load(filepath);
    CHECK_FALSE(load.document.has_value());
    CHECK(load.error.find("byte") != std::string::npos);
    std::filesystem::remove(filepath);
}

TEST_CASE("resolve_map_host: a dotted quad is returned unchanged",
          "[aun][map-writer]") {
    CHECK(resolve_map_host("127.0.0.1") == "127.0.0.1");
}

TEST_CASE("resolve_map_host: a name resolves (getaddrinfo needs Winsock on Windows)",
          "[aun][map-writer]") {
    // This runs with no bound AunBackend -- exactly the aun-map subcommand path
    // -- so the resolver must initialise Winsock itself on Windows, or
    // getaddrinfo fails with WSANOTINITIALISED and localhost comes back
    // unresolved (#149 Windows verification). localhost maps to the IPv4
    // loopback.
    auto resolved = resolve_map_host("localhost");
    REQUIRE(resolved.has_value());
    CHECK(*resolved == "127.0.0.1");
}

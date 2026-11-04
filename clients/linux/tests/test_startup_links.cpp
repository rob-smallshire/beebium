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

// The startup keyboard links (docs/preset-schema.md, startup): one byte seen
// four ways (screen mode and auto boot, the links one by one, hex and
// decimal), its layout entirely the schema's, written in the form the
// macOS client writes.

#include "preset/StartupLinks.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

namespace {

LinksLayout layout() {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-model-b.json");
    std::stringstream text;
    text << file.rdbuf();
    const auto schema = PresetSchema::parse(text.str());
    REQUIRE(schema);
    const auto found = LinksLayout::from_schema(*schema);
    REQUIRE(found);
    return *found;
}

}  // namespace

TEST_CASE("The layout is the schema's: eight links, the screen mode and boot bits",
          "[preset][links]") {
    const LinksLayout links = layout();
    REQUIRE(links.bits.size() == 8);
    CHECK(links.bits[0].bit == 7);
    CHECK(links.bits[0].link == "Link 1");
    CHECK(links.bits[0].label == "Filing system");
    CHECK(links.screen_mode_bits == std::vector<int>{0, 1, 2});
    CHECK(links.auto_boot_bit == 3);
    CHECK(links.default_byte == 255);
}

TEST_CASE("Screen mode and auto boot are bits of the byte", "[preset][links]") {
    const LinksLayout links = layout();
    // The documented example: mode 3 with auto boot is &F3.
    const StartupLinks startup =
        StartupLinks::from_json(links, ordered_json::parse(R"({"screen_mode": 3, "auto_boot": true})"));
    CHECK(startup.byte() == 0xF3);
    CHECK(startup.screen_mode() == 3);
    CHECK(startup.auto_boot());
    CHECK(startup.hex() == "F3");
    // Missing keys take the defaults: mode 7, no auto boot.
    CHECK(StartupLinks::from_json(links, ordered_json::object()).byte() == 255);
    CHECK(StartupLinks::from_json(links, ordered_json::parse(R"({"links": 135})")).byte() == 135);
}

TEST_CASE("A link is made when its bit is clear", "[preset][links]") {
    StartupLinks startup = StartupLinks::from_json(layout(), ordered_json::object());
    CHECK_FALSE(startup.link_made(7));
    startup.set_link_made(7, true);  // Link 1: NFS rather than DFS
    CHECK(startup.byte() == 0x7F);
    CHECK(startup.link_made(7));
    startup.set_screen_mode(0);
    CHECK(startup.byte() == 0x78);
    startup.set_auto_boot(true);
    CHECK(startup.byte() == 0x70);
}

TEST_CASE("The byte is written as screen mode and auto boot while the other links are open",
          "[preset][links]") {
    StartupLinks startup = StartupLinks::from_json(layout(), ordered_json::object());
    startup.set_screen_mode(3);
    startup.set_auto_boot(true);
    CHECK(startup.to_json() == ordered_json::parse(R"({"screen_mode": 3, "auto_boot": true})"));
    // Another link made: only the whole byte says it.
    startup.set_link_made(6, true);
    CHECK(startup.to_json() == ordered_json::parse(R"({"links": 179})"));
    // Open again: back to the friendly pair.
    startup.set_link_made(6, false);
    CHECK(startup.to_json().contains("screen_mode"));
}

TEST_CASE("Hex and decimal entries set the whole byte, within its range", "[preset][links]") {
    StartupLinks startup = StartupLinks::from_json(layout(), ordered_json::object());
    CHECK(startup.set_hex("87"));
    CHECK(startup.byte() == 0x87);
    CHECK(startup.set_hex("&f3"));
    CHECK(startup.byte() == 0xF3);
    CHECK(startup.set_decimal("135"));
    CHECK(startup.byte() == 135);
    CHECK_FALSE(startup.set_decimal("256"));
    CHECK_FALSE(startup.set_hex("1FF"));
    CHECK_FALSE(startup.set_hex("zz"));
    CHECK(startup.byte() == 135);  // unchanged by a refused entry
}

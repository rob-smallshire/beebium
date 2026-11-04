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

// What resolve-preset says of a preset (client specification 5.2): the
// machine it describes and its diagnostics, read as the server wrote them.

#include "preset/PresetResolution.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("A resolution carries the preset and each diagnostic as the server gave it",
          "[preset][resolution]") {
    const auto resolution = parse_resolution(R"({
      "preset": {"name": "BBC Model B", "model": "model-b", "speed": 1.0},
      "diagnostics": [
        {"severity": "warning", "path": "/colour", "location": "colour",
         "message": "unknown key 'colour' is ignored"},
        {"severity": "error", "path": "/extensions/1", "location": "Host Serial",
         "message": "preset extension 'host-serial': parameter 'baud' must be an integer, got 'fast'"}
      ]
    })");
    REQUIRE(resolution);
    REQUIRE(resolution->preset);
    CHECK((*resolution->preset)["model"] == "model-b");
    REQUIRE(resolution->diagnostics.size() == 2);
    CHECK_FALSE(resolution->diagnostics[0].error());
    CHECK(resolution->diagnostics[0].path == "/colour");
    CHECK(resolution->diagnostics[0].location == "colour");
    CHECK(resolution->diagnostics[1].error());
    CHECK(resolution->diagnostics[1].location == "Host Serial");
    CHECK(resolution->blocked());
}

TEST_CASE("Warnings alone do not block a launch", "[preset][resolution]") {
    const auto resolution = parse_resolution(R"({"preset": {"model": "model-b"},
      "diagnostics": [{"severity": "warning", "path": "/x", "location": "x", "message": "m"}]})");
    REQUIRE(resolution);
    CHECK_FALSE(resolution->blocked());
}

TEST_CASE("A preset that cannot be loaded has no document, only its errors",
          "[preset][resolution]") {
    const auto resolution = parse_resolution(R"({"preset": null, "diagnostics": [
      {"severity": "error", "path": "/econet/station", "location": "Econet",
       "message": "station 300 is out of range"}]})");
    REQUIRE(resolution);
    CHECK_FALSE(resolution->preset.has_value());
    CHECK(resolution->blocked());
}

TEST_CASE("Output that is not a resolution is nothing", "[preset][resolution]") {
    CHECK_FALSE(parse_resolution("").has_value());
    CHECK_FALSE(parse_resolution("Error: no such preset").has_value());
    CHECK_FALSE(parse_resolution("[1]").has_value());
}

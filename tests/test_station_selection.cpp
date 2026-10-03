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

#include <catch2/catch_test_macros.hpp>

#include <beebium/econet/StationSelection.hpp>

using namespace beebium::econet;

TEST_CASE("parse_station_spec accepts a fixed station", "[station-selection]") {
    std::string error;
    auto spec = parse_station_spec("80", error);
    REQUIRE(spec.has_value());
    CHECK_FALSE(spec->is_auto);
    CHECK(spec->fixed == 80);
    CHECK(error.empty());

    SECTION("range bounds 1 and 254 are valid") {
        CHECK(parse_station_spec("1", error)->fixed == 1);
        CHECK(parse_station_spec("254", error)->fixed == 254);
    }
}

TEST_CASE("parse_station_spec rejects a fixed station out of range",
          "[station-selection]") {
    std::string error;
    CHECK_FALSE(parse_station_spec("0", error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(parse_station_spec("255", error).has_value());
    CHECK_FALSE(parse_station_spec("999", error).has_value());
    CHECK_FALSE(parse_station_spec("", error).has_value());
    CHECK_FALSE(parse_station_spec("12x", error).has_value());
}

TEST_CASE("parse_station_spec accepts bare auto with the default range",
          "[station-selection]") {
    std::string error;
    auto spec = parse_station_spec("auto", error);
    REQUIRE(spec.has_value());
    CHECK(spec->is_auto);
    CHECK(spec->range.lo == kAutoStationDefaultRange.lo);
    CHECK(spec->range.hi == kAutoStationDefaultRange.hi);
    CHECK(spec->range.lo == 1);
    CHECK(spec->range.hi == 253);
    CHECK(error.empty());

    SECTION("auto is case-insensitive") {
        CHECK(parse_station_spec("AUTO", error)->is_auto);
        CHECK(parse_station_spec("Auto", error)->is_auto);
    }
}

TEST_CASE("parse_station_spec accepts auto with an explicit range",
          "[station-selection]") {
    std::string error;
    auto spec = parse_station_spec("auto:80-99", error);
    REQUIRE(spec.has_value());
    CHECK(spec->is_auto);
    CHECK(spec->range.lo == 80);
    CHECK(spec->range.hi == 99);

    SECTION("a single-number range lo==hi is valid") {
        auto one = parse_station_spec("auto:90-90", error);
        REQUIRE(one.has_value());
        CHECK(one->range.lo == 90);
        CHECK(one->range.hi == 90);
    }
}

TEST_CASE("parse_station_spec rejects malformed auto forms", "[station-selection]") {
    std::string error;
    CHECK_FALSE(parse_station_spec("auto:", error).has_value());
    CHECK_FALSE(parse_station_spec("auto:80", error).has_value());       // no dash
    CHECK_FALSE(parse_station_spec("auto80-99", error).has_value());     // no colon
    CHECK_FALSE(parse_station_spec("auto:99-80", error).has_value());    // lo > hi
    CHECK_FALSE(parse_station_spec("auto:0-99", error).has_value());     // lo out of range
    CHECK_FALSE(parse_station_spec("auto:80-255", error).has_value());   // hi out of range
    CHECK_FALSE(parse_station_spec("auto:a-z", error).has_value());      // non-numeric
}

TEST_CASE("lowest_free_station returns the first free number", "[station-selection]") {
    SECTION("nothing occupied -> the range's low number") {
        CHECK(lowest_free_station({}, {80, 253}) == 80);
    }
    SECTION("the low numbers taken -> the first gap") {
        CHECK(lowest_free_station({80, 81, 82}, {80, 253}) == 83);
    }
    SECTION("a hole below the top is filled first") {
        CHECK(lowest_free_station({80, 82}, {80, 253}) == 81);
    }
    SECTION("occupied numbers outside the range are ignored") {
        CHECK(lowest_free_station({1, 2, 254}, {80, 90}) == 80);
    }
}

TEST_CASE("lowest_free_station_from scans from a start with wraparound",
          "[station-selection]") {
    SECTION("from a start, nothing occupied -> the start") {
        CHECK(lowest_free_station_from({}, {1, 253}, 5) == 5);
    }
    SECTION("skips occupied numbers above the start") {
        CHECK(lowest_free_station_from({5, 6}, {1, 253}, 5) == 7);
    }
    SECTION("wraps past the top back to the bottom of the range") {
        // Start at the top, it is taken, so wrap to the free low numbers.
        CHECK(lowest_free_station_from({90}, {80, 90}, 90) == 80);
        CHECK(lowest_free_station_from({88, 89, 90}, {80, 90}, 88) == 80);
    }
    SECTION("a start outside the range is treated as range.lo") {
        CHECK(lowest_free_station_from({}, {80, 90}, 5) == 80);
        CHECK(lowest_free_station_from({}, {80, 90}, 200) == 80);
    }
    SECTION("exhausted range is nullopt regardless of start") {
        CHECK(lowest_free_station_from({80, 81, 82}, {80, 82}, 81) == std::nullopt);
    }
    SECTION("lowest_free_station is lowest_free_station_from at range.lo") {
        CHECK(lowest_free_station_from({80}, {80, 90}, 80) ==
              lowest_free_station({80}, {80, 90}));
    }
}

TEST_CASE("lowest_free_station reports an exhausted range", "[station-selection]") {
    CHECK(lowest_free_station({80, 81, 82}, {80, 82}) == std::nullopt);
    SECTION("a single-number range already taken is exhausted") {
        CHECK(lowest_free_station({90}, {90, 90}) == std::nullopt);
    }
    SECTION("a single free number in a full-looking range is found") {
        CHECK(lowest_free_station({80, 82}, {80, 82}) == 81);
    }
}

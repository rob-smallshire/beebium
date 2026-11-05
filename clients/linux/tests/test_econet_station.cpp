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

// A preset's Econet station read and written in its three forms, a typed
// number held within the schema's bounds.

#include "preset/EconetStation.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using nlohmann::ordered_json;

TEST_CASE("A station reads and writes as a number, auto, or auto in a range", "[preset][econet]") {
    const SchemaStation schema{1, 254, true, 1, 253};
    const auto fixed = EconetStation::from_json(80, schema);
    CHECK(fixed.mode == EconetStation::Mode::number);
    CHECK(fixed.to_json() == ordered_json(80));
    const auto automatic = EconetStation::from_json("auto", schema);
    CHECK(automatic.mode == EconetStation::Mode::automatic);
    CHECK(automatic.low == 1);
    CHECK(automatic.high == 253);
    CHECK(automatic.to_json() == ordered_json("auto"));
    const auto range = EconetStation::from_json("auto:10-20", schema);
    CHECK(range.mode == EconetStation::Mode::range);
    CHECK(range.low == 10);
    CHECK(range.high == 20);
    CHECK(range.to_json() == ordered_json("auto:10-20"));
}

TEST_CASE("A typed station is held within the schema's bounds", "[preset][econet]") {
    const SchemaStation schema{1, 254, true, 1, 253};
    CHECK(EconetStation::clamp("300", 80, schema) == 254);
    CHECK(EconetStation::clamp("0", 80, schema) == 1);
    CHECK(EconetStation::clamp(" 42 ", 80, schema) == 42);
    CHECK(EconetStation::clamp("forty", 80, schema) == 80);
}

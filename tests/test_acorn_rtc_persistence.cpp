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

// test_acorn_rtc_persistence.cpp
// Tests for the clock the acorn-rtc extension keeps on its battery
// (ExtensionPersistence): kept and taken back, running on by the time since,
// and refused for another register layout.

#include <catch2/catch_test_macros.hpp>

#include <AcornRtcExtension.hpp>
#include <beebium/extension/ExtensionPersistence.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace beebium;

namespace {

BatteryBackedUnit& clock_unit(AcornRtcExtension& ext) {
    REQUIRE(ext.persistence() != nullptr);
    auto units = ext.persistence()->battery_units();
    REQUIRE(units.size() == 1);
    return *units.front();
}

// `contents` as if kept `seconds_ago`: its time stamp moved back.
std::vector<uint8_t> kept_ago(std::vector<uint8_t> contents, std::int64_t seconds_ago) {
    std::uint64_t kept = 0;
    for (int i = 0; i < 8; ++i) kept |= static_cast<std::uint64_t>(contents[8 + i]) << (8 * i);
    kept -= static_cast<std::uint64_t>(seconds_ago);
    for (int i = 0; i < 8; ++i) contents[8 + i] = static_cast<uint8_t>(kept >> (8 * i));
    return contents;
}

}  // namespace

TEST_CASE("acorn-rtc keeps its clock as one battery-backed unit", "[acorn-rtc][persistence]") {
    AcornRtcExtension ext;
    ext.set_config({{"id", "clock"}, {"layout", "7bit-year-in-r7"}});
    auto& unit = clock_unit(ext);
    CHECK(unit.name() == "saf3019p");
    CHECK(unit.layout_version() == 1);
    CHECK(unit.hardware_descriptor() == "Acorn RTC: SAF3019P, layout 7bit-year-in-r7");
}

TEST_CASE("acorn-rtc takes back its registers, running on by the time since",
          "[acorn-rtc][persistence]") {
    AcornRtcExtension kept;
    kept.set_config({{"id", "clock"}, {"layout", "7bit-year-in-r7"}});
    kept.chip().set_layout(RegisterLayout::SevenBitYearInR7);
    REQUIRE(kept.chip().initialise({1995, 6, 1, 10, 0}));
    const auto contents = clock_unit(kept).serialise();
    REQUIRE(contents.size() == 17);

    AcornRtcExtension restored;
    restored.set_config({{"id", "clock"}, {"layout", "7bit-year-in-r7"}});
    restored.chip().set_layout(RegisterLayout::SevenBitYearInR7);
    REQUIRE(restored.chip().initialise({1985, 1, 1, 0, 0}));

    SECTION("at once: the time it was kept") {
        REQUIRE(clock_unit(restored).restore(contents));
        const auto now = restored.chip().current_datetime();
        CHECK(now.year == 1995);
        CHECK(now.month == 6);
        CHECK(now.day == 1);
        CHECK(now.hour == 10);
        CHECK(now.minute <= 1);
    }
    SECTION("two hours later: two hours on") {
        REQUIRE(clock_unit(restored).restore(kept_ago(contents, 2 * 3600)));
        const auto now = restored.chip().current_datetime();
        CHECK(now.day == 1);
        CHECK(now.hour == 12);
        CHECK(now.minute <= 1);
    }
}

TEST_CASE("acorn-rtc refuses contents it cannot hold", "[acorn-rtc][persistence]") {
    AcornRtcExtension ext;
    ext.set_config({{"id", "clock"}, {"layout", "7bit-year-in-r7"}});
    auto& unit = clock_unit(ext);
    auto contents = unit.serialise();
    contents.pop_back();
    CHECK_FALSE(unit.restore(contents));
    contents = unit.serialise();
    contents.back() = 60;  // a prescaler past a minute
    CHECK_FALSE(unit.restore(contents));
}

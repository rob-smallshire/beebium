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

// test_mc146818_battery_unit.cpp
// Tests for Mc146818BatteryUnit: an MC146818 clock as a battery-backed unit,
// with the name, descriptor and seed its board gives it.

#include <beebium/devices/Mc146818BatteryUnit.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace beebium;

namespace {

// A host clock that stands still, so offsets are exact.
std::chrono::microseconds fixed_host_time() {
    return std::chrono::seconds(1'700'000'000);
}

constexpr std::uint8_t SEEDED_RAM_VALUE = 0x5A;

}  // namespace

TEST_CASE("Mc146818BatteryUnit: name, descriptor and layout version", "[battery][rtc]") {
    Mc146818Rtc rtc(fixed_host_time);
    Mc146818BatteryUnit unit(rtc, "board-rtc", "Board: MC146818", [](Mc146818Rtc&) {});
    CHECK(unit.name() == "board-rtc");
    CHECK(unit.hardware_descriptor() == "Board: MC146818");
    CHECK(unit.layout_version() == 1);
}

TEST_CASE("Mc146818BatteryUnit: seed calls the board's seed with the clock and zeroes the offset",
          "[battery][rtc]") {
    Mc146818Rtc rtc(fixed_host_time);
    Mc146818Rtc* seeded = nullptr;
    Mc146818BatteryUnit unit(rtc, "board-rtc", "Board: MC146818", [&](Mc146818Rtc& clock) {
        seeded = &clock;
        auto image = clock.battery_image();
        image[Mc146818Rtc::FIRST_RAM_REGISTER] = SEEDED_RAM_VALUE;
        clock.load_battery_image(image);
        clock.set_clock_offset(std::chrono::seconds(1234));
    });

    unit.seed();

    CHECK(seeded == &rtc);
    CHECK(rtc.peek_register(Mc146818Rtc::FIRST_RAM_REGISTER) == SEEDED_RAM_VALUE);
    CHECK(rtc.clock_offset() == std::chrono::seconds(0));
    CHECK_FALSE(unit.restored());
}

TEST_CASE("Mc146818BatteryUnit: contents are the 64-byte image then an 8-byte offset",
          "[battery][rtc]") {
    Mc146818Rtc rtc(fixed_host_time);
    Mc146818BatteryUnit unit(rtc, "board-rtc", "Board: MC146818", [](Mc146818Rtc&) {});
    rtc.set_clock_offset(std::chrono::seconds(0x0102));

    const auto contents = unit.serialise();

    REQUIRE(contents.size() == 72);
    CHECK(contents[64] == 0x02);
    CHECK(contents[65] == 0x01);
    for (std::size_t i = 66; i < 72; ++i) CHECK(contents[i] == 0x00);
}

TEST_CASE("Mc146818BatteryUnit: a negative offset and the CMOS RAM round-trip",
          "[battery][rtc]") {
    Mc146818Rtc saved_rtc(fixed_host_time);
    Mc146818BatteryUnit saved(saved_rtc, "board-rtc", "Board: MC146818", [](Mc146818Rtc&) {});
    auto image = saved_rtc.battery_image();
    image[Mc146818Rtc::REGISTER_COUNT - 1] = 0xA5;
    saved_rtc.load_battery_image(image);
    saved_rtc.set_clock_offset(std::chrono::seconds(-86'400 * 400));
    const auto contents = saved.serialise();

    Mc146818Rtc rtc(fixed_host_time);
    Mc146818BatteryUnit unit(rtc, "board-rtc", "Board: MC146818", [](Mc146818Rtc&) {});
    REQUIRE(unit.restore(contents));

    CHECK(unit.restored());
    CHECK(rtc.clock_offset() == std::chrono::seconds(-86'400 * 400));
    CHECK(rtc.peek_register(Mc146818Rtc::REGISTER_COUNT - 1) == 0xA5);
    CHECK(unit.serialise() == contents);

    unit.seed();
    CHECK_FALSE(unit.restored());
}

TEST_CASE("Mc146818BatteryUnit: contents of the wrong size are refused, changing nothing",
          "[battery][rtc]") {
    Mc146818Rtc rtc(fixed_host_time);
    Mc146818BatteryUnit unit(rtc, "board-rtc", "Board: MC146818", [](Mc146818Rtc&) {});
    rtc.set_clock_offset(std::chrono::seconds(77));
    const auto before = unit.serialise();

    CHECK_FALSE(unit.restore(std::vector<std::uint8_t>(71, 0xFF)));
    CHECK_FALSE(unit.restore(std::vector<std::uint8_t>(73, 0xFF)));
    CHECK_FALSE(unit.restore({}));

    CHECK_FALSE(unit.restored());
    CHECK(unit.serialise() == before);
}

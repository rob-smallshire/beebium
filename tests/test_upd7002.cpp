// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// The NEC uPD7002 analogue-to-digital converter on its own, against
// docs/discussion/upd7002-adc-specification.md (tests 1 and 3 to 14; test 2,
// the mirror, needs the Model B memory map and lives with the Model B tests).

#include <catch2/catch_test_macros.hpp>

#include <beebium/ClockConcepts.hpp>
#include <beebium/devices/Upd7002.hpp>

#include <cstdint>

using beebium::Upd7002;

namespace {

constexpr uint16_t STATUS = 0;
constexpr uint16_t HIGH = 1;
constexpr uint16_t LOW = 2;
constexpr uint16_t LOW_AGAIN = 3;

constexpr uint8_t STATUS_NOT_EOC = 0x80;
constexpr uint8_t STATUS_NOT_BUSY = 0x40;
constexpr uint8_t STATUS_MSBS = 0x30;

constexpr uint8_t CONTROL_12_BIT = 0x08;
constexpr uint8_t CONTROL_FLAG = 0x04;

// Longer than any conversion the chip can take.
constexpr uint32_t CLOCK_LIMIT = 20000;

void run_clocks(Upd7002& adc, uint32_t clocks) {
    for (uint32_t i = 0; i < clocks; ++i) {
        adc.tick_falling();
    }
}

bool is_complete(const Upd7002& adc) {
    return (adc.peek(STATUS) & STATUS_NOT_EOC) == 0;
}

// Start a conversion and count the 1 MHz clocks until it completes.
uint32_t clocks_to_complete(Upd7002& adc, uint8_t control, uint16_t level) {
    adc.set_input_level(control & 0x03, level);
    adc.write(STATUS, control);
    for (uint32_t clocks = 1; clocks <= CLOCK_LIMIT; ++clocks) {
        adc.tick_falling();
        if (is_complete(adc)) {
            return clocks;
        }
    }
    return 0;
}

uint16_t level_for_code(uint16_t code12) {
    return static_cast<uint16_t>(code12 << 4);
}

void convert(Upd7002& adc, uint8_t control, uint16_t level) {
    REQUIRE(clocks_to_complete(adc, control, level) != 0);
}

}  // namespace

static_assert(beebium::StaticRateSubscriber<Upd7002>,
              "the uPD7002 is a 1 MHz clock subscriber");

TEST_CASE("uPD7002 power-on state reads status C0 with a zero result and CB1 high",
          "[upd7002]") {
    Upd7002 adc;
    CHECK(adc.read(STATUS) == 0xC0);
    CHECK(adc.peek(HIGH) == 0x00);
    CHECK(adc.peek(LOW) == 0x00);
    CHECK(adc.eoc_level() == 1);
}

TEST_CASE("uPD7002 status bits 3 to 0 keep the previous control until completion",
          "[upd7002]") {
    Upd7002 adc;
    adc.write(STATUS, CONTROL_12_BIT | 0x01);
    // The write changes only bits 7 and 6: the low bits still show the
    // power-on control, 0, until the conversion completes.
    CHECK((adc.read(STATUS) & 0x0F) == 0x00);
    run_clocks(adc, CLOCK_LIMIT);
    REQUIRE(is_complete(adc));
    CHECK((adc.read(STATUS) & 0x0F) == 0x09);

    adc.write(STATUS, 0xFE);  // bits 7 to 4 ignored: channel 2, 12-bit, flag
    CHECK((adc.read(STATUS) & 0x0F) == 0x09);
    run_clocks(adc, CLOCK_LIMIT);
    CHECK((adc.read(STATUS) & 0x0F) == 0x0E);

    adc.write(STATUS, 0x03);
    CHECK((adc.read(STATUS) & 0x0F) == 0x0E);
    run_clocks(adc, CLOCK_LIMIT);
    CHECK((adc.read(STATUS) & 0x0F) == 0x03);
}


TEST_CASE("uPD7002 is busy with EOC inactive until the last clock of a conversion",
          "[upd7002]") {
    Upd7002 adc;
    adc.set_input_level(0, 0x0000);
    adc.write(STATUS, CONTROL_12_BIT);  // code 0: 9100 clocks

    const uint8_t during_start = adc.read(STATUS);
    CHECK((during_start & STATUS_NOT_BUSY) == 0);
    CHECK((during_start & STATUS_NOT_EOC) != 0);
    CHECK(adc.eoc_level() == 1);

    run_clocks(adc, 9099);
    const uint8_t during_end = adc.read(STATUS);
    CHECK((during_end & STATUS_NOT_BUSY) == 0);
    CHECK((during_end & STATUS_NOT_EOC) != 0);
    CHECK(adc.eoc_level() == 1);

    run_clocks(adc, 1);
    const uint8_t after = adc.read(STATUS);
    CHECK((after & STATUS_NOT_BUSY) != 0);
    CHECK((after & STATUS_NOT_EOC) == 0);
    CHECK(adc.eoc_level() == 0);
}

TEST_CASE("uPD7002 status bits 5 and 4 hold the previous result during a conversion",
          "[upd7002]") {
    SECTION("zero then full scale") {
        Upd7002 adc;
        convert(adc, CONTROL_12_BIT, 0x0000);
        CHECK((adc.read(STATUS) & STATUS_MSBS) == 0x00);
        adc.set_input_level(0, 0xFFFF);
        adc.write(STATUS, CONTROL_12_BIT);
        CHECK((adc.read(STATUS) & STATUS_MSBS) == 0x00);
        run_clocks(adc, CLOCK_LIMIT);
        CHECK((adc.read(STATUS) & STATUS_MSBS) == 0x30);
    }
    SECTION("full scale then zero") {
        Upd7002 adc;
        convert(adc, CONTROL_12_BIT, 0xFFFF);
        CHECK((adc.read(STATUS) & STATUS_MSBS) == 0x30);
        adc.set_input_level(0, 0x0000);
        adc.write(STATUS, CONTROL_12_BIT);
        CHECK((adc.read(STATUS) & STATUS_MSBS) == 0x30);
        CHECK(adc.peek(HIGH) == 0xFF);
        run_clocks(adc, CLOCK_LIMIT);
        CHECK((adc.read(STATUS) & STATUS_MSBS) == 0x00);
        CHECK(adc.peek(HIGH) == 0x00);
    }
}

TEST_CASE("uPD7002 12-bit result is the high byte and the top nibble of the low byte",
          "[upd7002]") {
    Upd7002 adc;
    convert(adc, CONTROL_12_BIT, 0xABCD);
    CHECK(adc.read(LOW) == 0xC0);
    CHECK(adc.read(LOW_AGAIN) == 0xC0);
    CHECK(adc.peek(LOW_AGAIN) == adc.peek(LOW));
    CHECK(adc.read(HIGH) == 0xAB);
}

TEST_CASE("uPD7002 8-bit result has the same layout as the 12-bit one",
          "[upd7002]") {
    Upd7002 adc;
    convert(adc, 0x00, 0xABCD);
    CHECK(adc.read(LOW) == 0xC0);
    CHECK(adc.read(LOW_AGAIN) == 0xC0);
    CHECK(adc.read(HIGH) == 0xAB);
}


TEST_CASE("uPD7002 reading the high byte resets EOC and reading the others does not",
          "[upd7002]") {
    Upd7002 adc;
    convert(adc, CONTROL_12_BIT, 0x4000);
    REQUIRE(adc.eoc_level() == 0);

    (void)adc.read(STATUS);
    (void)adc.read(LOW);
    (void)adc.read(LOW_AGAIN);
    CHECK((adc.read(STATUS) & STATUS_NOT_EOC) == 0);
    CHECK(adc.eoc_level() == 0);

    CHECK(adc.read(HIGH) == 0x40);
    CHECK((adc.read(STATUS) & STATUS_NOT_EOC) != 0);
    CHECK((adc.read(STATUS) & STATUS_NOT_BUSY) != 0);
    CHECK(adc.eoc_level() == 1);
}

TEST_CASE("uPD7002 peek has no side effects", "[upd7002]") {
    Upd7002 adc;
    convert(adc, CONTROL_12_BIT, 0x4000);
    CHECK(adc.peek(HIGH) == 0x40);
    CHECK(adc.peek(STATUS) == adc.read(STATUS));
    CHECK((adc.peek(STATUS) & STATUS_NOT_EOC) == 0);
    CHECK(adc.eoc_level() == 0);
}

TEST_CASE("uPD7002 write during a conversion restarts it from the new write",
          "[upd7002]") {
    Upd7002 adc;
    adc.set_input_level(0, 0x0000);
    adc.set_input_level(1, 0xFFFF);
    adc.write(STATUS, 0x00);  // channel 0, 8-bit
    run_clocks(adc, 1000);

    adc.write(STATUS, CONTROL_12_BIT | 0x01);  // channel 1, 12-bit
    // The level is sampled at the write; a later change does not count.
    adc.set_input_level(1, 0x1234);

    // Code 4095 in 12-bit mode: tooth 7, 10553 + 2 * (4095 - 3796) = 11151.
    run_clocks(adc, 11150);
    CHECK_FALSE(is_complete(adc));
    run_clocks(adc, 1);
    REQUIRE(is_complete(adc));
    CHECK((adc.read(STATUS) & 0x0F) == (CONTROL_12_BIT | 0x01));
    CHECK(adc.read(LOW) == 0xF0);
    CHECK(adc.read(HIGH) == 0xFF);
}

TEST_CASE("uPD7002 8-bit conversion time follows the measured two-tooth line",
          "[upd7002]") {
    Upd7002 adc;
    CHECK(clocks_to_complete(adc, 0x00, level_for_code(0)) == 2945);
    CHECK(clocks_to_complete(adc, 0x00, level_for_code(2024)) == 3957);
    CHECK(clocks_to_complete(adc, 0x00, level_for_code(3436)) == 4663);
    CHECK(clocks_to_complete(adc, 0x00, level_for_code(3440)) == 4190);
    CHECK(clocks_to_complete(adc, 0x00, level_for_code(4080)) == 4510);
}


TEST_CASE("uPD7002 12-bit conversion time follows the measured sawtooth",
          "[upd7002]") {
    struct Tooth {
        uint16_t first_code;
        uint32_t first_clocks;
        uint16_t last_code;
        uint32_t measured_end_clocks;
    };
    // Last-code end times as measured (survey section 2.2); first codes and
    // start times from the specification's table.
    const Tooth teeth[] = {
        {0, 9100, 1016, 11138},
        {1020, 10118, 1448, 10975},
        {1452, 9957, 2048, 11153},
        {2052, 10139, 2624, 11281},
        {2628, 10265, 3192, 11386},
        {3204, 10387, 3784, 11560},
        {3796, 10553, 4080, 11120},
    };
    Upd7002 adc;
    for (const auto& tooth : teeth) {
        INFO("tooth starting at code " << tooth.first_code);
        CHECK(clocks_to_complete(adc, CONTROL_12_BIT, level_for_code(tooth.first_code))
              == tooth.first_clocks);
        const uint32_t end_clocks =
            clocks_to_complete(adc, CONTROL_12_BIT, level_for_code(tooth.last_code));
        const uint32_t difference = end_clocks > tooth.measured_end_clocks
                                        ? end_clocks - tooth.measured_end_clocks
                                        : tooth.measured_end_clocks - end_clocks;
        CHECK(difference <= 15);
    }
    const uint32_t before = clocks_to_complete(adc, CONTROL_12_BIT, level_for_code(1016));
    const uint32_t after = clocks_to_complete(adc, CONTROL_12_BIT, level_for_code(1024));
    CHECK(before > after);
    CHECK(before - after > 950);
}

TEST_CASE("uPD7002 unplugged channel reads 8000 after a 12-bit conversion",
          "[upd7002]") {
    Upd7002 adc;
    adc.write(STATUS, CONTROL_12_BIT | 0x02);
    run_clocks(adc, CLOCK_LIMIT);
    REQUIRE(is_complete(adc));
    CHECK(adc.read(LOW) == 0x00);
    CHECK(adc.read(HIGH) == 0x80);
}

TEST_CASE("uPD7002 writes to offsets 1, 2 and 3 change nothing observable",
          "[upd7002]") {
    Upd7002 adc;
    convert(adc, CONTROL_12_BIT | CONTROL_FLAG, 0x5670);
    const uint8_t status = adc.peek(STATUS);
    const uint8_t high = adc.peek(HIGH);
    const uint8_t low = adc.peek(LOW);

    for (uint16_t offset = 1; offset <= 3; ++offset) {
        adc.write(offset, 0xFF);
        adc.write(offset, 0x00);
    }
    CHECK(adc.peek(STATUS) == status);
    CHECK(adc.peek(HIGH) == high);
    CHECK(adc.peek(LOW) == low);
    CHECK(adc.eoc_level() == 0);

    // Nor do they disturb a conversion in progress.
    adc.write(STATUS, 0x00);  // code 0x567 = 1383, 8-bit: 2945 + 691 = 3636
    run_clocks(adc, 100);
    for (uint16_t offset = 1; offset <= 3; ++offset) {
        adc.write(offset, 0xFF);
    }
    run_clocks(adc, 3535);
    CHECK_FALSE(is_complete(adc));
    run_clocks(adc, 1);
    CHECK(is_complete(adc));
}

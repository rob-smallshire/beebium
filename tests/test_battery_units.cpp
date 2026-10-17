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

// test_battery_units.cpp
// Tests for a machine's battery-backed units (BatteryBackedUnit): which
// machines have them, the seed they start from, and their contents given up
// and taken back.

#include <beebium/IntegraBBatterySeed.hpp>
#include <beebium/Machines.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace beebium;

namespace {

BatteryBackedUnit& unit_named(std::vector<BatteryBackedUnit*> units, std::string_view name) {
    for (auto* unit : units) {
        if (unit->name() == name) return *unit;
    }
    FAIL("no unit " << name);
    throw;
}

}  // namespace

TEST_CASE("battery_units: a Model B keeps nothing; an Integra-B keeps its RAM and clock",
          "[battery]") {
    auto model_b = std::make_unique<ModelB>();
    CHECK(model_b->battery_units().empty());

    auto integra_b = std::make_unique<ModelBIntegraB>();
    std::vector<std::string> names;
    for (auto* unit : integra_b->battery_units()) {
        names.emplace_back(unit->name());
        CHECK(unit->layout_version() == 1);
        CHECK_FALSE(unit->hardware_descriptor().empty());
    }
    CHECK(names == std::vector<std::string>{"integra-b-board-ram", "integra-b-rtc"});
}

TEST_CASE("battery_units: an Integra-B starts from the seed of a board that has been set up",
          "[battery]") {
    auto machine = std::make_unique<ModelBIntegraB>();
    auto& board = machine->state().memory;
    // The clock's CMOS configuration, as IBOS's Full System Reset leaves it.
    // (Register B is not checked: the power-on reset clears its interrupt and
    // square-wave enables, as the chip's RESET line does.)
    const auto image = board.rtc.battery_image();
    for (std::size_t i = 0; i < sizeof(integra_b_battery_seed::CMOS_RAM); ++i) {
        CHECK(image[Mc146818Rtc::FIRST_RAM_REGISTER + i] == integra_b_battery_seed::CMOS_RAM[i]);
    }
    // The IBOS workspace in private RAM, and nothing in the board's banks.
    for (const auto& byte : integra_b_battery_seed::PRIVATE_RAM) {
        CHECK(board.shadow_and_private_ram.read(static_cast<uint16_t>(byte.address - 0x8000)) ==
              byte.value);
    }
    CHECK(board.sideways.peek_bank(5, 0x100) == 0x00);
}

TEST_CASE("battery_units: contents given up are taken back, after a seed or in another machine",
          "[battery]") {
    auto machine = std::make_unique<ModelBIntegraB>();
    auto& board = machine->state().memory;
    auto& ram = unit_named(machine->battery_units(), "integra-b-board-ram");
    auto& clock = unit_named(machine->battery_units(), "integra-b-rtc");

    // The board as a user has left it: shadow RAM, a sideways bank, the
    // clock's CMOS and its setting.
    board.shadow_and_private_ram.write(0x3000, 0xAB);
    board.sideways.write_bank(6, 0x2000, 0xCD);
    auto image = board.rtc.battery_image();
    image[Mc146818Rtc::FIRST_RAM_REGISTER + 20] = 0x5A;
    board.rtc.load_battery_image(image);
    board.rtc.set_clock_offset(std::chrono::hours(-24 * 365));
    const auto saved_ram = ram.serialise();
    const auto saved_clock = clock.serialise();
    CHECK(saved_ram.size() == 32768 + 4 * 16384);
    CHECK(saved_clock.size() == 64 + 8);

    // A seed returns the board to its first state...
    ram.seed();
    clock.seed();
    CHECK(board.shadow_and_private_ram.read(0x3000) == 0x00);
    CHECK(board.sideways.peek_bank(6, 0x2000) == 0x00);
    CHECK(board.rtc.battery_image()[Mc146818Rtc::FIRST_RAM_REGISTER + 20] == 0x00);

    // ...and the contents come back, here and in another machine alike.
    REQUIRE(ram.restore(saved_ram));
    REQUIRE(clock.restore(saved_clock));
    CHECK(ram.serialise() == saved_ram);
    CHECK(board.rtc.battery_image()[Mc146818Rtc::FIRST_RAM_REGISTER + 20] == 0x5A);
    CHECK(board.rtc.clock_offset().count() / 3600 == -24 * 365);

    auto other = std::make_unique<ModelBIntegraB>();
    REQUIRE(unit_named(other->battery_units(), "integra-b-board-ram").restore(saved_ram));
    CHECK(other->state().memory.sideways.peek_bank(6, 0x2000) == 0xCD);
}

TEST_CASE("battery_units: contents of the wrong size are refused and change nothing",
          "[battery]") {
    auto machine = std::make_unique<ModelBIntegraB>();
    for (auto* unit : machine->battery_units()) {
        INFO(unit->name());
        const auto before = unit->serialise();
        std::vector<std::uint8_t> wrong(before.size() - 1, 0xFF);
        CHECK_FALSE(unit->restore(wrong));
        CHECK(unit->serialise() == before);
    }
}

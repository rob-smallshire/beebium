// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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
#include <beebium/Machines.hpp>
#include <array>

using namespace beebium;

TEST_CASE("Machine template instantiation", "[machine][init]") {
    SECTION("ModelB can be instantiated") {
        ModelB machine;
        REQUIRE(machine.cycle_count() == 0);
    }

    SECTION("ModelBCmos can be instantiated") {
        ModelBCmos machine;
        REQUIRE(machine.cycle_count() == 0);
    }
}

TEST_CASE("Machine reset", "[machine][reset]") {
    ModelB machine;

    // Write something to memory
    machine.write(0x1000, 0x42);
    REQUIRE(machine.read(0x1000) == 0x42);

    // Reset
    machine.reset();
    REQUIRE(machine.read(0x1000) == 0x00);
    REQUIRE(machine.cycle_count() == 0);
}

TEST_CASE("Machine step and run", "[machine][execution]") {
    ModelB machine;

    // Create a minimal MOS with reset vector pointing to 0x0400
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);  // Fill with NOPs
    mos[0x3FFC] = 0x00;  // Reset vector low byte (offset from 0xC000)
    mos[0x3FFD] = 0x04;  // Reset vector high byte -> 0x0400
    machine.memory().load_mos(mos.data(), mos.size());

    // Simple program: NOP, NOP, NOP at address 0x0400
    machine.write(0x0400, 0xEA);  // NOP
    machine.write(0x0401, 0xEA);  // NOP
    machine.write(0x0402, 0xEA);  // NOP

    // Trigger CPU reset (but not full machine reset which would wipe MOS)
    M6502_Reset(&machine.cpu());

    // First step_instruction completes reset sequence (7 cycles)
    // CPU is now about to fetch opcode at 0x0400
    uint64_t cycles = machine.step_instruction();
    REQUIRE(cycles == 7);

    // Execute first NOP (2 cycles)
    cycles = machine.step_instruction();
    REQUIRE(cycles == 2);

    // Execute second NOP (2 cycles)
    cycles = machine.step_instruction();
    REQUIRE(cycles == 2);

    // Total should be 7 + 2 + 2 = 11
    REQUIRE(machine.cycle_count() == 11);

    // Run for a few more cycles
    uint64_t before = machine.cycle_count();
    machine.run(4);
    REQUIRE(machine.cycle_count() == before + 4);
}

TEST_CASE("Machine memory access", "[machine][memory]") {
    ModelB machine;

    SECTION("Read and write through machine interface") {
        machine.write(0x0200, 0xAB);
        REQUIRE(machine.read(0x0200) == 0xAB);
    }

    SECTION("Access memory policy directly") {
        REQUIRE(machine.memory().sideways.selected_bank() == 0);
    }
}

TEST_CASE("Machine state access", "[machine][state]") {
    ModelB machine;

    SECTION("State contains CPU") {
        auto& state = machine.state();
        REQUIRE(&state.cpu == &machine.cpu());
    }

    SECTION("State contains memory") {
        auto& state = machine.state();
        REQUIRE(&state.memory == &machine.memory());
    }

    SECTION("State contains cycle count") {
        machine.step();
        REQUIRE(machine.state().cycle_count == machine.cycle_count());
    }
}

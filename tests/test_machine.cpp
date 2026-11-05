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

    SECTION("ModelBPlus can be instantiated") {
        ModelBPlus machine;
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

    // Run for a few more cycles. A run ends at the first instruction boundary
    // at or after its target: the third NOP (2 cycles), then the BRK at &0403
    // (unwritten RAM) that starts inside the 4-cycle budget runs to its end.
    uint64_t before = machine.cycle_count();
    machine.run(4);
    REQUIRE(machine.cycle_count() == before + 2 + 7);
    REQUIRE_FALSE(machine.mid_instruction());
}

namespace {

// A Model B whose CPU is about to run, from &0400, sixteen NOPs, STA &0500,
// then NOPs to the end of the page. The MOS is NOPs with the reset vector at
// &0400.
void load_nop_slide_with_store(ModelB& machine) {
    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);
    mos[0x3FFC] = 0x00;
    mos[0x3FFD] = 0x04;
    machine.memory().load_mos(mos.data(), mos.size());
    for (uint16_t addr = 0x0400; addr < 0x0500; ++addr) machine.write(addr, 0xEA);
    machine.write(0x0410, 0x8D);  // STA &0500
    machine.write(0x0411, 0x00);
    machine.write(0x0412, 0x05);
    M6502_Reset(&machine.cpu());
    machine.step_instruction();  // the reset sequence
}

}  // namespace

// The sequence number counts every change to the machine, emulated cycles
// included, wherever a debugger can observe it: after a step, after a run,
// and inside a breakpoint or watchpoint callback part-way through a run.
TEST_CASE("Machine sequence counts each single step", "[machine][sequence]") {
    ModelB machine;
    load_nop_slide_with_store(machine);
    const uint64_t before = machine.sequence();
    machine.step();
    const uint64_t after_one = machine.sequence();
    CHECK(after_one > before);
    machine.step();
    CHECK(machine.sequence() > after_one);
}

TEST_CASE("Machine sequence counts the cycles a run executed", "[machine][sequence]") {
    ModelB machine;
    load_nop_slide_with_store(machine);
    const uint64_t sequence_before = machine.sequence();
    const uint64_t cycles_before = machine.cycle_count();
    machine.run(40);
    const uint64_t cycles_run = machine.cycle_count() - cycles_before;
    REQUIRE(cycles_run >= 40);
    CHECK(machine.sequence() - sequence_before >= cycles_run);
}

TEST_CASE("Machine sequence seen by a breakpoint callback counts the cycles before it",
          "[machine][sequence]") {
    ModelB machine;
    load_nop_slide_with_store(machine);
    const uint64_t sequence_before = machine.sequence();
    const uint64_t cycles_before = machine.cycle_count();
    uint64_t sequence_at_hit = 0;
    uint64_t cycles_at_hit = 0;
    machine.set_breakpoint_hit_callback([&](const BreakpointEntry&, uint32_t) {
        sequence_at_hit = machine.sequence();
        cycles_at_hit = machine.cycle_count();
    });
    machine.set_breakpoint_entries({BreakpointEntry{.id = 1, .start = 0x0410, .end = 0x0411}});
    machine.run(100);
    REQUIRE(cycles_at_hit > cycles_before);
    CHECK(sequence_at_hit - sequence_before >= cycles_at_hit - cycles_before);
}

TEST_CASE("Machine sequence seen by a watchpoint callback counts the cycles before it",
          "[machine][sequence]") {
    ModelB machine;
    load_nop_slide_with_store(machine);
    const uint64_t sequence_before = machine.sequence();
    const uint64_t cycles_before = machine.cycle_count();
    uint64_t sequence_at_hit = 0;
    uint64_t cycles_at_hit = 0;
    machine.set_watchpoint_hit_callback([&](const WatchpointEntry&, uint32_t, uint8_t, bool) {
        sequence_at_hit = machine.sequence();
        cycles_at_hit = machine.cycle_count();
    });
    machine.add_watchpoint_entry({
        .id = 1,
        .start = 0x0500,
        .end = 0x0501,
        .type = WATCH_WRITE,
        .condition = std::get<CompiledExpression>(compile("false")),
    });
    machine.run(100);
    REQUIRE(cycles_at_hit > cycles_before);
    CHECK(sequence_at_hit - sequence_before >= cycles_at_hit - cycles_before);
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

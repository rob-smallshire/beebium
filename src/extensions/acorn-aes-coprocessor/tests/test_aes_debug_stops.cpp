// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// test_aes_debug_stops.cpp, tests the debugger's breakpoints and watchpoints
// version 2.0.0, 20 November 2026.

// Breakpoints and watchpoints, over the rig in DebugRig.hpp. The design: docs/handoff.md at
// commit e9ac40f (step 4.1). Split from test_aes_debug.cpp at 500 lines (step 4.4b).

#include "DebugRig.hpp"

// Breakpoints and watchpoints (step 4.4b; design 4.1). The callbacks here do what the service's
// do on a stop (SRC, DebuggerService.hpp:481-520): count, then pause the target.

struct Stops {
    std::vector<uint32_t> breaks;  // the pc of each breakpoint hit
    struct Watch {
        uint32_t addr;
        uint8_t value;
        bool write;
    };
    std::vector<Watch> watches;
    void attach(AesDebug& debug) {
        debug.set_breakpoint_hit_callback([this, &debug](const beebium::BreakpointEntry&, uint32_t pc) {
            breaks.push_back(pc);
            debug.pause();
        });
        debug.set_watchpoint_hit_callback([this, &debug](const beebium::WatchpointEntry&, uint32_t addr, uint8_t value, bool write) {
            watches.push_back({addr, value, write});
            debug.pause();
        });
    }
};

beebium::BreakpointEntry break_at(uint32_t id, uint32_t at) { return {id, at, at + 1, false, {}, 0}; }
beebium::WatchpointEntry watch_on(uint32_t id, uint32_t from, uint32_t to, beebium::WatchType type) {
    return {id, from, to, type, false, {}, 0};
}

// Continue as the service's Run does: step over the breakpoints here, then resume.
void carry_on(DebugRig& rig) {
    rig.debug.step_over_breakpoints_once();
    rig.debug.resume();
}

TEST(debug_breakpoint_stops_before, "4.4b (a): a breakpoint stops the ARM before the instruction there: R15 is that address and the registers equal a twin core run to the same point (design 4.1; SRC, CpuDebugTarget.hpp:43-44)") {
    DebugRig rig(loop_rom()), twin(loop_rom());
    Stops stops;
    stops.attach(rig.debug);
    rig.debug.set_breakpoint_entries({break_at(1, 12)});
    rig.run(1000);
    CHECK_EQ(rig.debug.is_paused(), true);
    CHECK_EQ(stops.breaks.size(), 1);
    CHECK_EQ(stops.breaks.empty() ? 0 : stops.breaks[0], 12);
    CHECK_EQ(rig.value("R15"), 12);
    twin.core().step();  // the reset entry
    while (((twin.core().r15() & ArmCore::kPcMask) - 8) != 12) twin.core().step();
    CHECK_EQ(same_registers(rig.core(), twin.core()), true);
}

TEST(debug_several_breakpoints, "4.4b (a): several breakpoints at once each stop in turn (design 4.1)") {
    DebugRig rig(loop_rom());
    Stops stops;
    stops.attach(rig.debug);
    rig.debug.set_breakpoint_entries({break_at(1, 8), break_at(2, 16)});
    rig.run(1000);
    CHECK_EQ(rig.value("R15"), 8);
    carry_on(rig);
    rig.run(1000);
    CHECK_EQ(rig.value("R15"), 16);
    CHECK_EQ(stops.breaks.size(), 2);
}

TEST(debug_breakpoint_not_reached_or_deleted, "4.4b (b): a breakpoint never reached, and a deleted one, stop nothing (design 4.1)") {
    DebugRig rig(loop_rom());
    Stops stops;
    stops.attach(rig.debug);
    rig.debug.set_breakpoint_entries({break_at(1, 0x2000)});
    rig.run(1000);
    CHECK_EQ(rig.debug.is_paused(), false);
    rig.debug.set_breakpoint_entries({break_at(1, 12)});
    rig.debug.set_breakpoint_entries({});
    const uint64_t spent = rig.tube.ticks_spent();
    rig.run(1000);
    CHECK_EQ(rig.debug.is_paused(), false);
    CHECK_EQ(rig.tube.ticks_spent() > spent, true);
    CHECK_EQ(stops.breaks.size(), 0);
}

TEST(debug_step_over_once, "4.4b (c): resuming from a breakpoint runs its instruction once without stopping there again; it stops on the next pass (SRC, CpuDebugTarget.hpp:67-72)") {
    DebugRig rig(loop_rom());
    Stops stops;
    stops.attach(rig.debug);
    rig.debug.set_breakpoint_entries({break_at(1, 4)});  // the ADD R1,R1,#1
    rig.run(1000);
    CHECK_EQ(rig.value("R15"), 4);
    const uint32_t r1 = rig.value("R1");
    carry_on(rig);
    rig.run(1000);
    CHECK_EQ(stops.breaks.size(), 2);
    CHECK_EQ(rig.value("R15"), 4);
    CHECK_EQ(rig.value("R1"), r1 + 1);  // one pass of the loop
}

// Stores and loads at &12000, above 64 KB, from the ROM's own address (DOC, HW pp.34-35: the
// first write clears the latch, so the program runs at &3000000 up).
const uint32_t HIGH = 0x12000, AFTER_STR = ROM + 20, AFTER_LDRB = ROM + 24, AFTER_STM = ROM + 28, AFTER_LDM = ROM + 32;
std::vector<uint8_t> store_rom() {
    return rom_of({dp_imm(ADD, false, PC, PC, 3, 4), nop(),
                   dp_imm(MOV, false, 2, 0, 0x48, 11),   // R2 = &12000
                   dp_imm(MOV, false, 1, 0, 0x5A),       // R1 = &5A
                   ldr_imm(UP | PRE, 1, 2, 0),           // STR R1,[R2]
                   ldr_imm(UP | PRE | LOAD | BYTE, 3, 2, 1),  // LDRB R3,[R2,#1]
                   block(UP, 2, 0x30),                   // STMIA R2,{R4,R5}
                   block(UP | LOAD, 2, 0xC0),            // LDMIA R2,{R6,R7}
                   branch(32, 32)});
}

TEST(debug_watchpoints, "4.4b (d): read, write and both, matched in 32 bits above 64 KB, each word of a block transfer; the stop comes after the instruction (design 4.1)") {
    using beebium::WATCH_READ, beebium::WATCH_WRITE, beebium::WATCH_BOTH;
    struct Case {
        beebium::WatchpointEntry entry;
        uint32_t stop_after;
        uint32_t addr;
        bool write;
    };
    const Case cases[] = {
        {watch_on(1, HIGH, HIGH + 1, WATCH_WRITE), AFTER_STR, HIGH, true},        // STR, word
        {watch_on(2, HIGH + 1, HIGH + 2, WATCH_READ), AFTER_LDRB, HIGH + 1, false},  // LDRB
        {watch_on(3, HIGH + 4, HIGH + 5, WATCH_WRITE), AFTER_STM, HIGH + 4, true},   // STM's 2nd word
        {watch_on(4, HIGH + 7, HIGH + 8, WATCH_READ), AFTER_LDM, HIGH + 7, false},   // LDM's 2nd word, top byte
        {watch_on(5, HIGH, HIGH + 8, WATCH_BOTH), AFTER_STR, HIGH, true},          // the first access
        {watch_on(6, 0x2000, 0x2008, WATCH_BOTH), 0, 0, false},                  // &12000 cut to 16 bits: never
    };
    for (const Case& c : cases) {
        DebugRig rig(store_rom());
        rig.core().reg_in_mode(3, 4) = 0x44332211;
        rig.core().reg_in_mode(3, 5) = 0x88776655;
        Stops stops;
        stops.attach(rig.debug);
        rig.debug.set_watchpoint_entries({c.entry});
        rig.run(1000);
        CHECK_EQ(rig.debug.is_paused(), c.stop_after != 0);
        if (c.stop_after == 0) {
            CHECK_EQ(stops.watches.size(), 0);
            continue;
        }
        CHECK_EQ(rig.value("R15"), c.stop_after);
        CHECK_EQ(stops.watches.empty() ? 0 : stops.watches[0].addr, c.addr);
        CHECK_EQ(stops.watches.empty() ? 2 : stops.watches[0].write, c.write);
        CHECK_EQ(stops.watches.empty() ? 0 : stops.watches[0].value, rig.debug.peek_region("ram", c.addr));
    }
}

TEST(debug_set_running_cleared_paused, "4.4b (e): breakpoints and watchpoints set while running stop it; cleared while paused they stop nothing more (design 4.1)") {
    DebugRig rig(loop_rom());
    Stops stops;
    stops.attach(rig.debug);
    rig.run(1000);  // running, nothing set
    CHECK_EQ(rig.debug.is_paused(), false);
    rig.debug.set_breakpoint_entries({break_at(1, 12)});
    rig.run(1000);
    CHECK_EQ(rig.debug.is_paused(), true);
    rig.debug.set_breakpoint_entries({});
    carry_on(rig);
    rig.run(1000);
    CHECK_EQ(rig.debug.is_paused(), false);
    CHECK_EQ(stops.breaks.size(), 1);

    DebugRig store(store_rom());
    Stops store_stops;
    store_stops.attach(store.debug);
    store.run(3);  // started, not yet at its stores
    store.debug.set_watchpoint_entries({watch_on(1, HIGH, HIGH + 8, beebium::WATCH_BOTH)});
    store.run(1000);
    CHECK_EQ(store.debug.is_paused(), true);
    const size_t hits = store_stops.watches.size();  // one per byte the word store moved
    CHECK_EQ(hits, 4);
    store.debug.set_watchpoint_entries({});
    store.debug.resume();
    store.run(1000);
    CHECK_EQ(store.debug.is_paused(), false);
    CHECK_EQ(store_stops.watches.size(), hits);
}

TEST(debug_step_over_survives_interrupt, "resuming from a breakpoint with an IRQ pending: the handler runs first, then the breakpointed instruction runs once without a second stop (user guide, \"steps over it once\"; review, 21 Nov 2026)") {
    const uint8_t FLAG_I = 0x02;
    const uint32_t LOOP = 0x2C;
    DebugRig rig(rom_of({branch(0, 0x20), nop(), nop(), nop(), nop(), nop(),
                         ldr_imm(UP | PRE | LOAD | BYTE, 0, 12, 0),  // &18, IRQ: LDRB R0,[R12], which clears PIRQ
                         dp_imm(SUB, true, PC, 14, 4),               // SUBS PC,R14,#4
                         dp_imm(MOV, false, 12, 0, 1, 4),            // &20: R12 = &1000000, the Tube
                         dp_imm(ADD, false, 12, 12, 4),              // R12 = its R1 data register
                         dp_imm(TEQ, true, PC, PC, 3),               // TEQP PC,#3: SVC, IRQ unmasked (DOC, SW p.1)
                         dp_imm(ADD, false, 1, 1, 1),                // &2C: ADD R1,R1,#1
                         branch(0x30, LOOP)}));
    Stops stops;
    stops.attach(rig.debug);
    rig.debug.set_breakpoint_entries({break_at(1, LOOP)});
    rig.run(1000);
    CHECK_EQ(rig.value("R15"), LOOP);
    const uint32_t r1 = rig.value("R1");
    CHECK_EQ(rig.value("PSR") & (ArmCore::kI | 3), 3);
    rig.ula.host_write(H_R1STAT, 0x80 | FLAG_I);
    rig.ula.host_write(H_R1DATA, 0x20);  // PIRQ now pending
    carry_on(rig);
    rig.run(1000);
    CHECK_EQ(rig.value("R0"), 0x20);  // the handler ran
    CHECK_EQ(stops.breaks.size(), 2);
    CHECK_EQ(rig.value("R15"), LOOP);
    CHECK_EQ(rig.value("R1"), r1 + 1);  // the ADD ran once before the second stop
}

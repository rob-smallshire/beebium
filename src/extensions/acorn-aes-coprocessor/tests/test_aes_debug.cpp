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
// test_aes_debug.cpp, tests the debugger's registers, memory, steps and resets
// version 2.0.0, 20 November 2026.

// AesDebug, Beebium's debugger target, over an AesTube driven as Beebium drives it. Every
// expected value is read from the core itself, never through the debugger's own table. The
// design: docs/handoff.md at commit e9ac40f (step 4.1); the owner's answers, 18 Nov 2026.

#include "DebugRig.hpp"

TEST(debug_registers_match_core_in_each_mode, "4.2 (a): every register entry shows the core's own register, in user, FIQ, IRQ and SVC modes (design 4.1; DOC, SW p.1 banks)") {
    DebugRig rig(rom_of({branch(0, 0)}));
    rig.run(100);
    // Every physical register a different value, set in the core, not through the debugger.
    for (int mode = 0; mode < 4; mode++) {
        for (int n = 0; n < 15; n++) rig.core().reg_in_mode(mode, n) = 0xA0000000u | mode << 8 | n;
    }
    const auto list = every_banked_register();
    CHECK_EQ(rig.debug.cpu_descriptor().registers.size(), list.size() + 2);  // and R15, PSR
    for (uint32_t mode = 0; mode < 4; mode++) {
        rig.enter_mode(mode);
        for (const Named& r : list) {
            const uint32_t expected = rig.core().reg_in_mode(r.mode < 0 ? int(mode) : r.mode, r.n);
            const size_t index = rig.index_of(r.name);
            CHECK_EQ(index < rig.debug.cpu_descriptor().registers.size(), true);
            CHECK_EQ(rig.value(r.name), expected);
        }
    }
}

TEST(debug_r15_is_the_next_instruction, "4.2 (b): R15 is the address of the instruction about to run, not the fetch address (design 4.1; DOC, SW p.1: R15 runs 8 ahead)") {
    DebugRig rig(rom_of({nop(), branch(4, 4)}));  // Spins on the branch at 4.
    rig.run(500);
    CHECK_EQ(rig.value("R15"), 4);
    // The word there is the one the core will run next.
    CHECK_EQ(rig.core().next_instruction(), branch(4, 4));
    CHECK_EQ(rig.debug.cpu_descriptor().registers[rig.index_of("R15")].role == beebium::cpu::RegisterRole::ProgramCounter, true);
}

TEST(debug_psr_is_read_only, "4.2 (c): a PSR write is ignored and the read-back shows the true flags and mode (owner, 18 Nov 2026)") {
    DebugRig rig(rom_of({branch(0, 0)}));
    rig.run(100);
    rig.enter_mode(2);
    rig.core().set_r15(rig.core().r15() | 0xA0000000u);  // N and C set
    const uint32_t r15 = rig.core().r15();
    CHECK_EQ(rig.value("PSR"), r15 & (FLAGS | MODE_BITS));
    rig.set("PSR", 0);
    CHECK_EQ(rig.core().r15(), r15);
    CHECK_EQ(rig.value("PSR"), r15 & (FLAGS | MODE_BITS));
}

TEST(debug_r15_write_is_quiet, "4.2 (d): writing R15 moves the next instruction there with no bus cycle, no tick and no Tube access (design 4.1)") {
    DebugRig rig(rom_of({branch(0, 0)}));
    rig.run(100);
    const uint64_t ticks = rig.tube.ticks_spent();
    const long accesses = rig.ula.accesses;
    const uint32_t psr = rig.value("PSR");
    rig.set("R15", TUBE);  // A fetch there would be a Tube access.
    CHECK_EQ(rig.value("R15"), TUBE);
    CHECK_EQ(rig.tube.ticks_spent(), ticks);
    CHECK_EQ(rig.ula.accesses, accesses);
    CHECK_EQ(rig.value("PSR"), psr);
    rig.set("R15", 0x3000000);  // The ROM's own address: its first word runs next.
    CHECK_EQ(rig.value("R15"), 0x3000000);
    CHECK_EQ(rig.core().next_instruction(), branch(0, 0));
    CHECK_EQ(rig.tube.ticks_spent(), ticks);
}

TEST(debug_register_writes_read_back, "4.2 (e): R0-R14 and a bank register, set through the debugger, read back from the core (design 4.1)") {
    DebugRig rig(rom_of({branch(0, 0)}));
    rig.run(100);
    rig.enter_mode(3);
    for (int n = 0; n < 15; n++) rig.set("R" + std::to_string(n), 0x5000 + n);
    for (int n = 0; n < 15; n++) {
        CHECK_EQ(rig.core().reg_in_mode(3, n), 0x5000 + n);
        CHECK_EQ(rig.value("R" + std::to_string(n)), 0x5000 + n);
    }
    rig.set("R13_irq", 0x1234);
    CHECK_EQ(rig.core().reg_in_mode(2, 13), 0x1234);
    CHECK_EQ(rig.value("R13_irq"), 0x1234);
    CHECK_EQ(rig.core().reg_in_mode(3, 13), 0x500D);  // SVC's own R13 untouched.
}

TEST(debug_cycle_count_is_ticks, "4.2 (f): cycle_count is the plugin's own crystal-tick count, and sequence counts instructions (owner, 18 Nov 2026)") {
    DebugRig rig(rom_of({nop(), branch(4, 4)}));
    rig.run(500);
    CHECK_EQ(rig.tube.ticks_spent() > 0, true);
    CHECK_EQ(rig.debug.cycle_count(), rig.tube.ticks_spent());
    const uint64_t before = rig.debug.sequence();
    rig.run(500);
    CHECK_EQ(rig.debug.cycle_count(), rig.tube.ticks_spent());
    CHECK_EQ(rig.debug.sequence() > before, true);
}

TEST(debug_signals, "4.2: IRQ and FIQ lines as the core last sampled them; pending only when not masked (design 4.1, INF)") {
    DebugRig rig(rom_of({branch(0, 0)}));
    rig.run(100);
    CHECK_EQ(rig.debug.cpu_descriptor().signals.size(), 2);
    rig.core().set_irq(true);
    rig.enter_mode(3);  // I and F set: masked.
    CHECK_EQ(rig.debug.signal_state(0).asserted, true);
    CHECK_EQ(rig.debug.signal_state(0).pending, false);
    CHECK_EQ(rig.debug.signal_state(1).asserted, false);
    rig.core().set_r15(rig.core().r15() & ~ArmCore::kI);
    CHECK_EQ(rig.debug.signal_state(0).pending, true);
    rig.enter_mode(2);
    CHECK_EQ(rig.debug.signal_state(0).in_handler, true);
    CHECK_EQ(rig.debug.signal_state(1).in_handler, false);
}

// Memory (step 4.3; design 4.1). The core's own view is AesMemory's read_byte, the path the
// ARM's own loads take; peek must agree with it and change nothing.

std::vector<uint8_t> counting_rom() {
    std::vector<uint8_t> rom = rom_of({branch(0, 0)});
    for (size_t i = 4; i < rom.size(); i++) rom[i] = uint8_t(i * 7 + 3);
    return rom;
}

const beebium::MemoryRegionDescriptor* region(const std::vector<beebium::MemoryRegionDescriptor>& list, std::string_view name) {
    for (const auto& r : list) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

TEST(debug_regions, "4.3 (a): ram, tube and rom listed at their addresses for 2 and 4 MB, never with side effects (design 4.1; DOC, HW 5.1.5)") {
    for (int mb : {2, 4}) {
        AesOptions o;
        o.ram = mb;
        DebugRig rig(counting_rom(), o);
        const auto list = rig.debug.get_memory_regions();
        CHECK_EQ(list.size(), 3);
        const auto* ram = region(list, "ram");
        const auto* tube = region(list, "tube");
        const auto* rom = region(list, "rom");
        CHECK_EQ(ram && tube && rom, true);
        if (!ram || !tube || !rom) continue;
        CHECK_EQ(ram->base_address, 0);
        CHECK_EQ(ram->size, uint32_t(mb) << 20);
        CHECK_EQ(tube->base_address, TUBE);
        CHECK_EQ(tube->size, 32);
        CHECK_EQ(rom->base_address, ROM);
        CHECK_EQ(rom->size, 0x4000);
        using F = beebium::RegionFlags;
        for (const auto* r : {ram, tube, rom}) {
            CHECK_EQ(has_flag(r->flags, F::Readable) && has_flag(r->flags, F::Populated), true);
            CHECK_EQ(has_flag(r->flags, F::HasSideEffects), false);
        }
        CHECK_EQ(has_flag(ram->flags, F::Writable), true);
        CHECK_EQ(has_flag(tube->flags, F::Writable) || has_flag(rom->flags, F::Writable), false);
        // Under the boot latch the low addresses read the ROM, so RAM is not what a read sees there.
        CHECK_EQ(has_flag(ram->flags, F::Active), false);
        rig.tube.memory().write_byte(0x100, 1, false);  // the first write clears the latch
        CHECK_EQ(has_flag(region(rig.debug.get_memory_regions(), "ram")->flags, F::Active), true);
    }
}

TEST(debug_peek_is_the_cpu_view, "4.3 (b): peek and read show what the ARM's own read shows, under the latch and after it clears; the ram region shows RAM itself (design 4.1; DOC, HW pp.34-35 boot latch)") {
    DebugRig rig(counting_rom());
    AesMemory& memory = rig.tube.memory();
    const uint32_t places[] = {0x0, 0x21, 0x3FFF, 0x8000, 0x1FFFFF, 0x3FFFFF, ROM, ROM + 0x21, ROM + 0x3FFF, REGION10 + 0x10};
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t at : places) {
            const uint8_t seen = rig.debug.peek(at);
            CHECK_EQ(rig.debug.read(at), seen);
            CHECK_EQ(seen, memory.read_byte(at, false));  // the core's own view
        }
        CHECK_EQ(memory.boot_latch_set(), pass == 0);
        // The ram region is RAM itself, latch or not; rom is the ROM.
        CHECK_EQ(rig.debug.peek_region("ram", 0x21), 0);
        CHECK_EQ(rig.debug.peek_region("rom", ROM + 0x21), uint8_t(0x21 * 7 + 3));
        CHECK_EQ(rig.debug.read_region("rom", ROM + 0x21), uint8_t(0x21 * 7 + 3));
        if (pass == 0) memory.write_byte(0x8000, 0x77, false);  // clears the latch
    }
    CHECK_EQ(rig.debug.peek(0x8000), 0x77);
    CHECK_EQ(rig.debug.peek_region("ram", 0x8000), 0x77);
    bool refused = false;
    try {
        rig.debug.peek_region("ram", 4u << 20);  // past 4 MB
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    CHECK_EQ(refused, true);
}

TEST(debug_peek_changes_nothing, "4.3 (c): with a byte waiting in R1, peeking every Tube address leaves the byte, the flags, the ticks, the latch and the ROM as they were (design 4.1; SRC, TubeUla.hpp coprocessor_peek)") {
    DebugRig rig(counting_rom());
    rig.run(100);
    rig.ula.host_write(H_R1DATA, 0x42);
    uint8_t before[8];
    for (int reg = 0; reg < 8; reg++) before[reg] = rig.ula.coprocessor_peek(uint8_t(reg));
    const uint64_t ticks = rig.tube.ticks_spent();
    const bool latch = rig.tube.memory().boot_latch_set();
    const std::vector<uint8_t> rom = counting_rom();
    for (uint32_t at = TUBE; at < TUBE + 32; at++) {
        rig.debug.peek(at);
        rig.debug.read(at);
        rig.debug.peek_region("tube", at);
        rig.debug.read_region("tube", at);
    }
    CHECK_EQ(rig.debug.peek(TUBE + 4), 0x42);  // R1's data, as the ARM would read it
    CHECK_EQ(rig.debug.peek(TUBE), before[0]);  // R1's status, data waiting
    for (int reg = 0; reg < 8; reg++) CHECK_EQ(rig.ula.coprocessor_peek(uint8_t(reg)), before[reg]);
    CHECK_EQ(rig.ula.accesses, 0);
    CHECK_EQ(rig.tube.ticks_spent(), ticks);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), latch);
    int rom_same = 0;
    for (uint32_t i = 0; i < 0x4000; i++) rom_same += rig.debug.peek(ROM + i) == rom[i];
    CHECK_EQ(rom_same, 0x4000);
    CHECK_EQ(rig.ula.coprocessor_read(1), 0x42);  // still there for the ARM
}

TEST(debug_writes_ram_only, "4.3 (d): a debugger write reaches RAM without moving the latch or the ticks; ROM and Tube writes are refused and change nothing (design 4.1)") {
    DebugRig rig(counting_rom());
    AesMemory& memory = rig.tube.memory();
    const uint64_t ticks = rig.tube.ticks_spent();
    rig.debug.write(0x40, 0xAB);  // flat, under the latch
    CHECK_EQ(memory.boot_latch_set(), true);
    CHECK_EQ(rig.debug.peek(0x40), uint8_t(0x40 * 7 + 3));  // the CPU still sees the ROM there
    CHECK_EQ(rig.debug.peek_region("ram", 0x40), 0xAB);
    rig.debug.write_region("ram", 0x8001, 0xCD);
    CHECK_EQ(rig.debug.peek(0x8001), counting_rom()[0x8001 & 0x3FFF]);  // still under the latch: the ROM repeats there
    CHECK_EQ(rig.debug.peek_region("ram", 0x8001), 0xCD);
    CHECK_EQ(memory.boot_latch_set(), true);
    CHECK_EQ(rig.tube.ticks_spent(), ticks);
    memory.write_byte(0x100, 1, false);  // clear the latch, as the ARM's first write does
    CHECK_EQ(rig.debug.peek(0x40), 0xAB);
    CHECK_EQ(rig.debug.peek(0x8001), 0xCD);

    rig.ula.host_write(H_R1DATA, 0x42);
    const uint8_t r1_status = rig.ula.coprocessor_peek(0);
    rig.debug.write(ROM + 0x21, 0);
    rig.debug.write(TUBE + 4, 0);
    rig.debug.write(TUBE, 0);
    int refusals = 0;
    for (const char* name : {"rom", "tube"}) {
        try {
            rig.debug.write_region(name, name[0] == 'r' ? ROM + 0x21 : TUBE + 4, 0);
        } catch (const std::invalid_argument&) {
            refusals++;
        }
    }
    CHECK_EQ(refusals, 2);
    CHECK_EQ(rig.debug.peek(ROM + 0x21), uint8_t(0x21 * 7 + 3));
    CHECK_EQ(rig.ula.coprocessor_peek(0), r1_status);
    CHECK_EQ(rig.ula.accesses, 0);
    CHECK_EQ(rig.ula.coprocessor_read(1), 0x42);
}

// Steps and resets (step 4.4a; design 4.1; OI-42, owner, 18 Nov 2026).

TEST(debug_steps_match_the_core, "4.4a (a): N debugger steps leave every register as the core's own N steps do; sequence +1 and cycle_count + that instruction's ticks each step; stepped time comes off the next batch (design 4.1)") {
    DebugRig stepped(loop_rom()), direct(loop_rom());
    stepped.debug.pause();
    const uint64_t due = stepped.tube.ticks_due();
    for (int i = 0; i < 25; i++) {
        const uint64_t seq = stepped.debug.sequence(), cycles = stepped.debug.cycle_count();
        const uint64_t direct_before = direct.tube.ticks_spent();
        const uint64_t returned = stepped.debug.step_instruction();
        direct.core().step();
        const uint64_t cost = direct.tube.ticks_spent() - direct_before;
        CHECK_EQ(stepped.debug.sequence(), seq + 1);
        CHECK_EQ(stepped.debug.cycle_count(), cycles + cost);
        CHECK_EQ(returned, cost);
        CHECK_EQ(stepped.debug.mid_instruction(), false);
    }
    CHECK_EQ(same_registers(stepped.core(), direct.core()), true);
    CHECK_EQ(stepped.core().reg_in_mode(3, 1) > 5, true);  // the loop ran
    stepped.debug.step();  // the single-cycle step is a whole instruction here
    direct.core().step();
    CHECK_EQ(same_registers(stepped.core(), direct.core()), true);
    // Nothing became due by stepping; after resume nothing runs until the steps are paid off.
    CHECK_EQ(stepped.tube.ticks_due(), due);
    stepped.debug.resume();
    const uint64_t spent = stepped.tube.ticks_spent();
    stepped.run(1);
    CHECK_EQ(stepped.tube.ticks_spent(), spent);
}

TEST(debug_reset_is_the_plugin_reset, "4.4a (b): the debugger's Reset, the plugin's reset then one step, leaves the ARM as the plugin's own reset and one step do; RAM kept (design 4.1; SRC, DebuggerService.hpp Reset)") {
    DebugRig a(loop_rom()), b(loop_rom());
    for (DebugRig* rig : {&a, &b}) {
        rig->run(500);
        rig->tube.memory().write_byte(0x8000, 0x5A, false);  // RAM, and the latch cleared
    }
    a.debug.pause();
    a.debug.reset();
    a.debug.step_instruction();
    b.tube.reset();
    b.core().step();
    CHECK_EQ(same_registers(a.core(), b.core()), true);
    CHECK_EQ(a.value("R15"), 0);  // at the reset vector
    CHECK_EQ(a.value("PSR") & 0x0C000003, 0x0C000003);  // SVC, I and F set (DOC, SW p.28)
    CHECK_EQ(a.tube.memory().boot_latch_set(), true);
    CHECK_EQ(a.debug.peek_region("ram", 0x8000), 0x5A);
}

// The ARM in its reset state, seen while still paused: the latch set again, and its next
// instruction the reset entry (one debugger step: SVC, I and F set, at the vector).
void check_reset_seen_while_paused(DebugRig& rig) {
    CHECK_EQ(rig.debug.is_paused(), true);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), true);
    CHECK_EQ(rig.debug.peek(0), loop_rom()[0]);
    rig.debug.step_instruction();
    CHECK_EQ(rig.value("R15"), 0);
    CHECK_EQ(rig.value("PSR") & 0x0C000003, 0x0C000003);
}

TEST(debug_p_ending_in_pause_restarts, "4.4a (c), OI-42: P set and cleared wholly inside a pause restarts the ARM at once, seen before resuming (owner, 18 Nov 2026)") {
    DebugRig rig(loop_rom());
    rig.run(1000);
    rig.tube.memory().write_byte(0x8000, 0x5A, false);  // the latch cleared, as the Executive's first write does
    rig.debug.pause();
    rig.ula.host_write(H_R1STAT, SET_P);
    rig.run(100);
    rig.ula.host_write(H_R1STAT, CLEAR_P);
    rig.run(100);
    check_reset_seen_while_paused(rig);
    CHECK_EQ(rig.debug.peek_region("ram", 0x8000), 0x5A);
}

TEST(debug_break_in_pause_restarts, "4.4a (c): Break (Beebium's reset) during a pause shows the ARM in its reset state before resuming (owner, 19 Nov 2026; DOC, Beebium contract, Reset)") {
    DebugRig rig(loop_rom());
    rig.run(1000);
    rig.tube.memory().write_byte(0x8000, 0x5A, false);
    rig.debug.pause();
    rig.tube.reset();
    check_reset_seen_while_paused(rig);
}

TEST(debug_p_unpaused_unchanged, "4.4a (c): the same P scenario without a pause ends as before: the ARM restarted and running (guard; tube.prst_level default)") {
    DebugRig rig(loop_rom());
    rig.run(1000);
    rig.tube.memory().write_byte(0x8000, 0x5A, false);
    rig.ula.host_write(H_R1STAT, SET_P);
    rig.run(100);
    rig.ula.host_write(H_R1STAT, CLEAR_P);
    rig.run(100);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), true);  // restarted: low addresses read the ROM
    CHECK_EQ(rig.core().reg_in_mode(3, 1) >= 5, true);  // and running the loop again
    CHECK_EQ(rig.value("PSR") & 3, 3);
}

TEST(debug_p_held_through_pause, "4.4a (c): P held through a whole pause restarts nothing until it ends (OI-42; tube.prst_level default)") {
    DebugRig rig(loop_rom());
    rig.run(1000);
    rig.tube.memory().write_byte(0x8000, 0x5A, false);
    rig.debug.pause();
    rig.ula.host_write(H_R1STAT, SET_P);
    rig.run(100);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    rig.debug.resume();
    const uint64_t spent = rig.tube.ticks_spent();
    rig.run(100);
    CHECK_EQ(rig.tube.ticks_spent(), spent);  // held
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    rig.ula.host_write(H_R1STAT, CLEAR_P);
    rig.run(1);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), true);  // restarted when P ended
}

TEST(debug_step_takes_interrupts, "4.5: a debugger step samples PIRQ as IRQ and PNMI as FIQ, as the running loop does: unmasked, the step enters IRQ or FIQ mode (design 4.1; INF, profile tube.irq_wiring; DOC, AN004 pp.12-14)") {
    const uint8_t FLAG_I = 0x02, FLAG_M = 0x08;
    const int H_R3DATA = 5;
    for (const bool fiq : {false, true}) {
        DebugRig rig(loop_rom());
        rig.run(100);
        rig.core().set_r15(rig.core().r15() & ~(ArmCore::kI | ArmCore::kF));  // SVC, unmasked
        rig.debug.pause();
        if (fiq) {
            rig.ula.host_write(H_R1STAT, 0x80 | FLAG_M);
            rig.ula.host_write(H_R3DATA, 0x20);
            rig.ula.host_write(H_R3DATA, 0x21);
        } else {
            rig.ula.host_write(H_R1STAT, 0x80 | FLAG_I);
            rig.ula.host_write(H_R1DATA, 0x20);
        }
        CHECK_EQ(rig.value("PSR") & 3, 3);
        rig.debug.step_instruction();
        rig.debug.step_instruction();
        CHECK_EQ(rig.value("PSR") & 3, fiq ? 1 : 2);
    }
}

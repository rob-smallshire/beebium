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
// test_aes_tube.cpp, tests the Tube link and the time budget
// version 2.0.0, 20 November 2026.

// AesTube: the ARM side of the Tube, driven as Beebium drives it, with Beebium's Tube chip
// linked and called. The chip is apparatus here, not evidence. Each test names the document
// its expected result rests on: Beebium's contract for time, AN004 for the Tube.

#include "../beebium/AesTube.hpp"
#include "ArmTest.hpp"

#include <beebium/tube/TubeUla.hpp>

#include <algorithm>

using beebium::TubeUla;

static const uint32_t TUBE = 0x1000000, R1DATA = TUBE + 4;
static const int WORK = 1, BASE = 2;
static const uint32_t LOOP = 0x8000;

// Host-side offsets and flags: DOC, AN004 pp.11-13. Status writes: bit 7 sets the named flags.
static const int H_R1STAT = 0, H_R1DATA = 1, H_R3DATA = 5;
static const uint8_t SET = 0x80, FLAG_I = 0x02, FLAG_M = 0x08, FLAG_P = 0x20;

static std::vector<uint8_t> rom_with(std::initializer_list<uint32_t> words) {
    std::vector<uint8_t> rom(0x4000);
    size_t at = 0;
    for (uint32_t w : words) {
        for (int i = 0; i < 4; i++) rom[at++] = uint8_t(w >> (8 * i));
    }
    return rom;
}

// R2 = the Tube's base, built with MOV: &1000000 is 1 ror 8 (DOC, SW p.11).
static uint32_t base_of_tube() { return dp_imm(MOV, false, BASE, 0, 1, 4); }
static uint32_t spin(uint32_t at) { return branch(at, at); }
// A write clears the boot latch and low addresses become RAM (DOC, HW pp.34-35), so a program
// that writes first jumps to the ROM's own address: &3000008, the word after next.
static uint32_t jump_to_high_rom() { return dp_imm(ADD, false, PC, PC, 3, 4); }
static uint32_t pc_of(AesTube& tube) { return tube.core().r15() & 0x3FFFFFC; }
static uint32_t add_one(int reg) { return dp_imm(ADD, false, reg, reg, 1); }

// A count of every Tube access's lateness, seen as the chip sees it.
struct WatchedUla : TubeUla {
    beebium::Coprocessor* owner = nullptr;
    long accesses = 0, early = 0, longest_late = 0;
    void note();
    uint8_t coprocessor_read(uint8_t offset) override {
        note();
        return TubeUla::coprocessor_read(offset);
    }
    void coprocessor_write(uint8_t offset, uint8_t value) override {
        note();
        TubeUla::coprocessor_write(offset, value);
    }
};

// The instant this sees: the end of the access cycle. AesMemory charges the cycle, its wait
// included, and any refresh starting at its end, before it calls the chip. So `late` < 0 means
// the access ended after the time due. It says nothing about the instruction's later cycles:
// tube_whole_instruction_ends_in_time checks those.
void WatchedUla::note() {
    const AesTube& tube = *static_cast<AesTube*>(owner);
    const long late = long(tube.ticks_due()) - long(tube.ticks_spent());
    accesses++;
    if (late < 0) early++;
    if (late > longest_late) longest_late = late;
}

struct TubeRig {
    WatchedUla ula;
    AesOptions options;
    AesTube tube;
    uint64_t now = 1000;

    explicit TubeRig(const std::vector<uint8_t>& rom, AesOptions o = AesOptions()) : options(o), tube(options, ula) {
        ula.owner = &tube;
        tube.set_rom(rom);
        tube.run_until(now);
    }
    void run(uint64_t host_cycles, uint64_t batch = 8) {
        for (uint64_t done = 0; done < host_cycles; done += batch) {
            now += batch;
            tube.run_until(now);
        }
    }
    uint32_t& r(int n) { return tube.core().reg_in_mode(tube.core().r15() & 3, n); }
    uint32_t mode() { return tube.core().r15() & 3; }
};

TEST(tube_first_call_sets_origin, "The first run_until sets the origin with nothing due, however late it comes (DOC, Beebium contract, Time and Origin)") {
    TubeUla ula;
    AesOptions o;
    AesTube tube(o, ula);
    tube.set_rom(rom_with({spin(0)}));
    tube.run_until(5000000);
    CHECK_EQ(tube.ticks_due(), 0);
    CHECK_EQ(tube.ticks_spent(), 0);
    tube.run_until(5000100);
    CHECK_EQ(tube.ticks_due(), 1000);
    CHECK_EQ(tube.ticks_spent() >= 1000, true);
}

TEST(tube_ticks_are_ten_per_host_cycle, "Ticks due are exactly 10 per host cycle in any batching, and spent ticks stay within one instruction of them (INF, 20 MHz over 2 MHz; DOC, Beebium guide, exact cycles)") {
    TubeRig rig(rom_with({add_one(WORK), spin(4)}));
    const uint64_t batches[] = {1, 8, 3, 7, 2, 5};
    uint64_t total = 0;
    for (int round = 0; round < 500; round++) {
        for (uint64_t batch : batches) {
            rig.run(batch, batch);
            total += batch;
            CHECK_EQ(rig.tube.ticks_due(), 10 * total);
            const long gap = long(rig.tube.ticks_spent()) - long(rig.tube.ticks_due());
            CHECK_EQ(gap >= 0 && gap < 200, true);
        }
    }
}

TEST(tube_same_time_runs_nothing, "A second run_until at the same time runs nothing (DOC, Beebium contract, Time)") {
    TubeRig rig(rom_with({add_one(WORK), spin(4)}));
    rig.run(100);
    const uint64_t spent = rig.tube.ticks_spent();
    CHECK_EQ(spent >= 1000, true);
    rig.tube.run_until(rig.now);
    rig.tube.run_until(rig.now);
    CHECK_EQ(rig.tube.ticks_spent(), spent);
}

TEST(tube_pause_loses_time, "While paused nothing runs and the time is lost, not deferred (DOC, Beebium contract, Pause)") {
    TubeRig rig(rom_with({add_one(WORK), spin(4)}));
    rig.run(100);
    rig.tube.pause();
    CHECK_EQ(rig.tube.is_paused(), true);
    const uint64_t spent = rig.tube.ticks_spent();
    const uint64_t due = rig.tube.ticks_due();
    rig.run(1000);
    CHECK_EQ(rig.tube.ticks_spent(), spent);
    rig.tube.resume();
    CHECK_EQ(rig.tube.is_paused(), false);
    rig.run(80);
    CHECK_EQ(rig.tube.ticks_due(), due + 800);
}

TEST(tube_reset_restarts_and_keeps_ram, "reset() restarts at the ROM with the latch set and keeps RAM; host time may go backwards (DOC, Beebium contract, Origin and reset; INF, DRAM keeps its data, notes/beebium-integration.md section 1)") {
    TubeRig rig(rom_with({jump_to_high_rom(), nop(), dp_imm(MOV, false, WORK, 0, 0x5A),
                       dp_imm(MOV, false, BASE, 0, 1, 12), ldr_imm(UP | PRE, WORK, BASE, 0), spin(20)}));
    rig.run(1000);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    CHECK_EQ(rig.tube.memory().peek(0x100), 0x5A);
    CHECK_EQ(pc_of(rig.tube) >> 24, 3);
    rig.tube.memory().write_byte(0x200, 0x55, false);
    rig.tube.reset();
    CHECK_EQ(rig.tube.memory().boot_latch_set(), true);
    rig.now = 10;  // A hard reset zeroes the host count (DOC, Beebium contract).
    rig.tube.run_until(rig.now);
    rig.run(1, 1);
    CHECK_EQ(pc_of(rig.tube) < 0x100, true);
    CHECK_EQ(rig.mode(), 3);  // DOC, SW p.28: reset enters SVC mode.
    // Low addresses read the ROM until the latch clears, so RAM can only be seen after it.
    rig.run(1000);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    CHECK_EQ(rig.tube.memory().peek(0x200), 0x55);
}

TEST(tube_strict_lax_after_reset_and_prst, "abort strict: Beebium's reset and the end of a P reset both put checking back to lax, and the program's user mode starts it again (USER, 4 Nov 2026; DOC, AN004 pp.12-13: P resets the parasite)") {
    AesOptions o;
    o.abort = AesOptions::MachineAbort::Strict;
    TubeRig rig(rom_with({jump_to_high_rom(), nop(), dp_imm(TEQ, true, PC, PC, 0), spin(12)}), o);
    rig.run(1000);
    CHECK_EQ(rig.mode(), 0);
    CHECK_EQ(rig.tube.memory().strict_on(), true);
    rig.tube.reset();
    CHECK_EQ(rig.tube.memory().strict_on(), false);
    rig.now = 10;
    rig.tube.run_until(rig.now);
    rig.run(1000);
    CHECK_EQ(rig.tube.memory().strict_on(), true);
    rig.ula.host_write(H_R1STAT, SET | FLAG_P);
    rig.run(8);
    const uint64_t spent = rig.tube.ticks_spent();
    rig.ula.host_write(H_R1STAT, FLAG_P);
    for (int i = 0; i < 10 && rig.tube.ticks_spent() == spent; i++) rig.run(1, 1);
    CHECK_EQ(rig.mode(), 3);
    CHECK_EQ(rig.tube.memory().strict_on(), false);
    rig.run(1000);
    CHECK_EQ(rig.tube.memory().strict_on(), true);
}

TEST(tube_register_spacing, "The ARM's addresses reach the chip's registers word-spaced: R1DATA at &1000004 (SRC, Kernel:101-108; DOC, HW 5.3, A2-A4)") {
    TubeRig rig(rom_with({base_of_tube(), dp_imm(MOV, false, WORK, 0, 0x41),
                       ldr_imm(UP | PRE | BYTE, WORK, BASE, R1DATA - TUBE), spin(12)}));
    rig.run(100);
    CHECK_EQ(rig.ula.host_read(H_R1STAT) & 0x80, 0x80);  // DOC, AN004 p.11: bit 7 is data ready.
    CHECK_EQ(rig.ula.host_read(H_R1DATA), 0x41);
}

TEST(tube_pirq_is_irq, "PIRQ reaches the ARM's IRQ, sampled as a level: a byte in R1 with I set enters the vector at &18 in IRQ mode (INF, profile tube.irq_wiring; DOC, AN004 pp.12-13; DOC, SW pp.29-30)") {
    // Clearing I in user mode is a TEQP; the ROM's vector &18 spins, and the main code counts.
    std::vector<uint8_t> rom = rom_with({branch(0, 0x40), nop(), nop(), nop(), nop(), nop(), spin(0x18), spin(0x1C)});
    const std::vector<uint8_t> main = rom_with({dp_imm(TEQ, true, PC, 0, 0, 0), add_one(WORK), spin(0x48)});
    std::copy(main.begin(), main.begin() + 12, rom.begin() + 0x40);
    TubeRig rig(rom);
    rig.run(100);
    CHECK_EQ(rig.mode(), 0);
    rig.ula.host_write(H_R1STAT, SET | FLAG_I);
    rig.ula.host_write(H_R1DATA, 0x20);
    rig.run(100);
    CHECK_EQ(rig.mode(), 2);
    CHECK_EQ(pc_of(rig.tube) - 8, 0x18);
}

TEST(tube_pnmi_is_fiq, "PNMI reaches the ARM's FIQ, sampled as a level: R3 data with M set enters the vector at &1C in FIQ mode (INF, profile tube.irq_wiring; DOC, AN004 pp.13-14; DOC, SW pp.29-30)") {
    std::vector<uint8_t> rom = rom_with({branch(0, 0x40), nop(), nop(), nop(), nop(), nop(), spin(0x18), spin(0x1C)});
    const std::vector<uint8_t> main = rom_with({dp_imm(TEQ, true, PC, 0, 0, 0), add_one(WORK), spin(0x48)});
    std::copy(main.begin(), main.begin() + 12, rom.begin() + 0x40);
    TubeRig rig(rom);
    rig.run(100);
    rig.ula.host_write(H_R1STAT, SET | FLAG_M);
    rig.ula.host_write(H_R3DATA, 0x20);
    rig.ula.host_write(H_R3DATA, 0x21);
    rig.run(100);
    CHECK_EQ(rig.mode(), 1);
    CHECK_EQ(pc_of(rig.tube) - 8, 0x1C);
}

// A status poll in a loop: the shape of every Tube wait in the Executive (SRC, Kernel:234-237).
static std::vector<uint8_t> poll_loop() {
    return rom_with({base_of_tube(), ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, 0),
                     dp_imm(TST, true, 0, WORK, 0x80), if_cond(0, branch(12, 4)), spin(16)});
}

TEST(tube_access_never_ahead_of_host, "A polling load's Tube access ends inside the time due, in any batching, so the host never sees an access from its future (DOC, Beebium guide section 6 and contract Step 3b; design, item 27)") {
    for (uint64_t batch : {uint64_t(1), uint64_t(3), uint64_t(8)}) {
        TubeRig rig(poll_loop());
        rig.run(20000, batch);
        CHECK_EQ(rig.ula.accesses > 500, true);
        CHECK_EQ(rig.ula.early, 0);
    }
}

TEST(tube_block_access_never_ahead_of_host, "So does every access of an LDM of several words at the Tube, from ROM at the ROM's wait (DOC, Beebium guide section 6; design, item 27)") {
    const uint32_t list = 0x00F0;
    TubeRig rig(rom_with({base_of_tube(), block(LOAD | UP, BASE, list), branch(8, 4)}));
    rig.run(20000, 8);
    CHECK_EQ(rig.ula.accesses > 100, true);
    CHECK_EQ(rig.ula.early, 0);
}

TEST(tube_register_offset_never_ahead, "So does the access of a load whose address only shows through a register offset: R1 = &1000000, base 0 (DOC, Beebium guide section 6; design, item 27)") {
    TubeRig rig(rom_with({dp_imm(MOV, false, WORK, 0, 1, 4), dp_imm(MOV, false, BASE, 0, 0),
                       ldr_reg(LOAD | UP | PRE | BYTE, 3, BASE, WORK), branch(12, 8)}));
    rig.run(20000, 8);
    CHECK_EQ(rig.ula.accesses > 100, true);
    CHECK_EQ(rig.ula.early, 0);
}

// A long run of one instruction, every word of it touching the Tube. So whatever ran last
// when run_until returns is a Tube instruction, and an overrun there shows at once.
static void check_whole_instructions(uint32_t instruction, uint64_t batch) {
    TubeRig rig(rom_with({spin(0)}));
    const int kWords = 6000;
    for (int i = 0; i < kWords; i++) rig.tube.memory().write_word(LOOP + 4 * i, instruction, false);
    rig.tube.core().set_r15(0x0C000003 | LOOP);
    rig.r(BASE) = TUBE;
    rig.r(4) = 0;
    rig.r(5) = 0;
    while (rig.tube.ticks_due() < rig.tube.ticks_spent()) rig.run(8);  // Pays off the loading.
    long overruns = 0;
    rig.ula.accesses = 0;
    for (uint64_t done = 0; done < 8000; done += batch) {
        rig.run(batch, batch);
        overruns += rig.tube.ticks_spent() > rig.tube.ticks_due();
    }
    CHECK_EQ(rig.ula.accesses > 200, true);
    if (overruns) printf("  instruction %08X, batch %d: %ld overruns\n", instruction, int(batch), overruns);
    CHECK_EQ(overruns, 0);
}

TEST(tube_whole_instruction_ends_in_time, "A Tube instruction ends inside the time due, last cycle and all, not just its access: load, store, register offset, register shift and LDM, in batches of 1 and 8 (DOC, Beebium guide section 6 and contract Step 3b)") {
    for (uint64_t batch : {uint64_t(1), uint64_t(8)}) {
        check_whole_instructions(ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, 0), batch);
        check_whole_instructions(ldr_imm(UP | PRE | BYTE, WORK, BASE, R1DATA - TUBE), batch);
        check_whole_instructions(ldr_reg(LOAD | UP | PRE | BYTE, WORK, 4, BASE), batch);
        check_whole_instructions(ldr_rs(LOAD | UP | PRE | BYTE, WORK, 4, BASE, LSL, 5), batch);
        check_whole_instructions(block(LOAD | UP, BASE, 0x0F00), batch);
    }
}

TEST(tube_wait_reads_the_pipeline, "A Tube load already fetched is held as the pipeline holds it, though the program has since written a no-op over it in memory: its access never lands ahead of the host (what-if 29; DOC, SW p.1: the instruction runs as fetched)") {
    for (uint64_t batch : {uint64_t(1), uint64_t(3)}) {
        TubeRig rig(rom_with({spin(0)}));
        const uint32_t load = ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, 0);
        const uint32_t no_op = dp_reg(MOV, false, 0, 0, 0);
        // LOOP: store the no-op over LOOP+8, which is already fetched; a no-op; the Tube load
        // (as fetched); put the load back; and round again.
        const uint32_t words[] = {ldr_imm(UP | PRE, 6, 7, 0), no_op, load, ldr_imm(UP | PRE, 8, 7, 0),
                                  branch(LOOP + 16, LOOP)};
        for (int i = 0; i < 5; i++) rig.tube.memory().write_word(LOOP + 4 * i, words[i], false);
        rig.tube.core().set_r15(0x0C000003 | LOOP);
        rig.r(BASE) = TUBE;
        rig.r(6) = no_op;
        rig.r(7) = LOOP + 8;
        rig.r(8) = load;
        while (rig.tube.ticks_due() < rig.tube.ticks_spent()) rig.run(8);  // Pays off the loading.
        rig.ula.accesses = 0;
        rig.ula.early = 0;
        rig.run(20000, batch);
        CHECK_EQ(rig.ula.accesses > 100, true);
        if (rig.ula.early) printf("  batch %d: %ld of %ld accesses early\n", int(batch), rig.ula.early, rig.ula.accesses);
        CHECK_EQ(rig.ula.early, 0);
    }
}

TEST(tube_ordinary_code_is_not_held, "Instructions away from the Tube are not held for it, and may overshoot by one (DOC, Beebium contract Step 3b)") {
    TubeRig rig(rom_with({jump_to_high_rom(), nop(), dp_imm(MOV, false, BASE, 0, 1, 8), add_one(WORK),
                       ldr_imm(UP | PRE, WORK, BASE, 0), branch(20, 12)}));
    rig.run(1000, 1);
    CHECK_EQ(rig.r(WORK) > 3, true);
    const long gap = long(rig.tube.ticks_spent()) - long(rig.tube.ticks_due());
    CHECK_EQ(gap >= 0 && gap < 100, true);
}

TEST(tube_interrupt_is_not_held_up, "A pending FIQ is taken at once, even when the next instruction is one waiting for the Tube: the instruction does not run, so it need not fit (DOC, SW pp.29-30)") {
    std::vector<uint8_t> rom = rom_with({branch(0, 0x40), nop(), nop(), nop(), nop(), nop(), nop(), spin(0x1C)});
    const std::vector<uint8_t> main = rom_with({dp_imm(TEQ, true, PC, 0, 0, 0), base_of_tube(),
                                                ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, 0), branch(0x4C, 0x48)});
    std::copy(main.begin(), main.begin() + 16, rom.begin() + 0x40);
    TubeRig rig(rom);
    rig.run(200);
    // Time left due after a run shows the load is held: nothing else in this program holds.
    while (rig.tube.ticks_spent() >= rig.tube.ticks_due()) rig.run(1, 1);
    rig.ula.host_write(H_R1STAT, SET | FLAG_M);
    rig.ula.host_write(H_R3DATA, 0x20);
    rig.ula.host_write(H_R3DATA, 0x21);
    // 10 ticks. This load's price from ROM is 54: 30 fetch, 12 Tube, 3 last, 9 refresh.
    rig.run(1, 1);
    CHECK_EQ(rig.mode(), 1);
}

TEST(tube_p_flag_holds_arm, "tube.prst_level default: P = 1 holds the ARM while it is set, RAM kept, and it restarts from the ROM when P clears (DOC rough scan, HW 5.3 pin 37 to ARM RST; INF that it holds; DOC, AN004 pp.12-13: P resets the parasite)") {
    TubeRig rig(rom_with({jump_to_high_rom(), nop(), dp_imm(MOV, false, WORK, 0, 0x5A),
                       dp_imm(MOV, false, BASE, 0, 1, 12), ldr_imm(UP | PRE, WORK, BASE, 0), spin(20)}));
    rig.run(1000);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    rig.tube.memory().write_byte(0x200, 0x55, false);
    rig.ula.host_write(H_R1STAT, SET | FLAG_P);
    rig.run(8);
    const uint64_t spent = rig.tube.ticks_spent();
    rig.run(1000);
    CHECK_EQ(rig.tube.ticks_spent(), spent);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    rig.ula.host_write(H_R1STAT, FLAG_P);
    // An overshoot from before the hold is paid off first. This program's dearest instruction
    // is the branch: 3 ROM cycles of 30 ticks and one refresh of 9, so 99 ticks, and 10 host
    // cycles pay that off and start the next step, the restart. Arithmetic, not a choice, on
    // defaults that are INF: time.rom_wait, time.refresh_cost (profile, section 6).
    for (int i = 0; i < 10 && rig.tube.ticks_spent() == spent; i++) rig.run(1, 1);
    CHECK_EQ(rig.tube.ticks_spent() > spent, true);
    CHECK_EQ(pc_of(rig.tube) < 0x100, true);
    CHECK_EQ(rig.mode(), 3);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), true);
    // Low addresses read the ROM until the latch clears, so RAM can only be seen after it.
    rig.run(1000);
    CHECK_EQ(rig.tube.memory().boot_latch_set(), false);
    CHECK_EQ(rig.tube.memory().peek(0x200), 0x55);
}

TEST(tube_p_flag_pulse_alternative, "tube.prst_level pulse: the ARM runs on while P is set and restarts when P clears (DOC, SM6502 p.31; INF for the AES)") {
    AesOptions o;
    o.tube_prst_level = AesOptions::TubePrstLevel::Pulse;
    TubeRig rig(rom_with({add_one(WORK), spin(4)}), o);
    rig.run(1000);
    rig.ula.host_write(H_R1STAT, SET | FLAG_P);
    const uint64_t spent = rig.tube.ticks_spent();
    rig.run(100);
    CHECK_EQ(rig.tube.ticks_spent() > spent, true);
    CHECK_EQ(rig.r(WORK), 1);
    rig.ula.host_write(H_R1STAT, FLAG_P);
    rig.r(WORK) = 7;
    rig.run(100);
    CHECK_EQ(rig.r(WORK), 8);  // Back at &0: the first instruction ran again.
}

// Clears the latch, puts the words in RAM and starts them in SVC mode with interrupts off, as
// the Executive's IRQ routine runs (SRC, Kernel:1018-1024).
static void start_in_ram(TubeRig& rig, uint32_t at, std::initializer_list<uint32_t> words) {
    uint32_t to = at;
    for (uint32_t w : words) {
        rig.tube.memory().write_word(to, w, false);
        to += 4;
    }
    rig.tube.core().set_r15(0x0C000003 | at);
}

// Runs what is loaded, in batches of 1, and counts the batches ending with time still due.
// Only an instruction held for the Tube leaves any: otherwise the loop runs past what is due.
static long held_batches(TubeRig& rig) {
    while (rig.tube.ticks_due() < rig.tube.ticks_spent()) rig.run(8);
    long held = 0;
    for (int i = 0; i < 2000; i++) {
        rig.run(1, 1);
        held += rig.tube.ticks_spent() < rig.tube.ticks_due();
    }
    return held;
}

static long held_load(uint32_t flags, uint32_t base, uint32_t offset) {
    TubeRig rig(rom_with({spin(0)}));
    start_in_ram(rig, LOOP, {ldr_imm(LOAD | PRE | flags, WORK, BASE, offset), branch(LOOP + 4, LOOP)});
    rig.r(BASE) = base;
    return held_batches(rig);
}

TEST(tube_reach_below_the_tube, "A load below the Tube is held out to the edge of the 4,096 reach and not past it, and the last word that really reaches the chip does not land early (DOC, SW p.19, a 12-bit offset; design choice, profile 8a)") {
    CHECK_EQ(held_load(UP, 0xFFF000, 0) > 0, true);   // Just inside: + 4,096 is &1000000.
    CHECK_EQ(held_load(UP, 0xFFEFFC, 0), 0);          // Just outside: + 4,096 is &FFFFFC.
    TubeRig rig(rom_with({spin(0)}));
    start_in_ram(rig, LOOP, {ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, 0xFFC), branch(LOOP + 4, LOOP)});
    rig.r(BASE) = 0xFFF004;
    rig.run(500, 8);
    rig.ula.accesses = rig.ula.early = 0;
    rig.run(2000, 1);
    CHECK_EQ(rig.ula.accesses > 50, true);
    CHECK_EQ(rig.ula.early, 0);
}

TEST(tube_reach_above_the_tube, "A load above the Tube is held out to the edge of the 4,096 reach and not past it (DOC, SW p.19, a 12-bit offset; design choice, profile 8a)") {
    CHECK_EQ(held_load(0, 0x2000FFC, 0) > 0, true);  // Just inside: - 4,096 is &1FFFFFC.
    CHECK_EQ(held_load(0, 0x2001000, 0), 0);         // Just outside: - 4,096 is &2000000.
}

TEST(tube_r15_base_is_eight_ahead, "An R15-based load counts from the address it really uses, 8 ahead: at &FFEFFC with offset &FFC it reaches &1000000 and must be held; at &FFEFF4 it is out of reach (DOC, SW p.1, SW p.19; design choice, profile 8a)") {
    const uint32_t at = 0xFFEFFC;
    TubeRig rig(rom_with({spin(0)}));
    start_in_ram(rig, at, {ldr_imm(LOAD | UP | PRE | BYTE, WORK, PC, 0xFFC), branch(at + 4, at)});
    rig.run(500, 8);
    rig.ula.accesses = rig.ula.early = 0;
    rig.run(2000, 1);
    CHECK_EQ(rig.ula.accesses > 50, true);
    CHECK_EQ(rig.ula.early, 0);

    const uint32_t away = 0xFFEFF4;
    TubeRig other(rom_with({spin(0)}));
    start_in_ram(other, away, {ldr_imm(LOAD | UP | PRE | BYTE, WORK, PC, 0), branch(away + 4, away)});
    CHECK_EQ(held_batches(other), 0);
}

TEST(tube_stm_ends_in_time, "An STM of several words at the Tube ends inside the time due, in batches of 1 and 8 (DOC, Beebium guide section 6 and contract Step 3b)") {
    for (uint64_t batch : {uint64_t(1), uint64_t(8)}) {
        check_whole_instructions(block(UP, BASE, 0x00F0), batch);
    }
}

// FIQP7, word for word (SRC, Kernel:1018-1024): R3STAT bit 7, then R3DATA, then store.
static const uint32_t TO = 0x10000;
static const uint32_t R3STAT_OFFSET = 16, R3DATA_OFFSET = 20;
static void start_type_7(TubeRig& rig) {
    start_in_ram(rig, LOOP, {ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, R3STAT_OFFSET),
                             dp_imm(TST, true, 0, WORK, 0x80),
                             if_cond(0, branch(LOOP + 8, LOOP)),
                             ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, R3DATA_OFFSET),
                             ldr_imm(UP | BYTE, WORK, 0, 1),
                             dp_imm(SUB, true, 3, 3, 1),
                             if_cond(1, branch(LOOP + 24, LOOP)),
                             spin(LOOP + 28)});
    rig.r(0) = TO;
    rig.r(BASE) = TUBE;
    rig.r(3) = 256;
}

// The host's loop with `pace` host cycles a byte, run as Beebium's socket runs it: batches of
// at most 8, and the ARM brought up to time before each host access (DOC, Beebium contract).
static int bytes_kept_at_pace(int pace) {
    TubeRig rig(rom_with({spin(0)}));
    start_type_7(rig);
    rig.run(200);
    for (int i = 0; i < 256; i++) {
        for (int left = pace; left > 0; left -= 8) {
            rig.now += std::min(left, 8);
            rig.tube.run_until(rig.now);
        }
        rig.ula.host_write(H_R3DATA, uint8_t(i + 1));
    }
    rig.run(2000);
    int kept = 0;
    for (int i = 0; i < 256; i++) kept += rig.tube.memory().peek(TO + i) == uint8_t(i + 1);
    return kept;
}

TEST(tube_type7_at_an004_pace, "A type 7 transfer through the Executive's own loop keeps every byte at AN004's 10 us a byte, 20 host cycles (DOC, AN004 pp.9, 27-28: read each byte before the next host write; SRC, Kernel FIQP7)") {
    CHECK_EQ(bytes_kept_at_pace(20), 256);
}

TEST(tube_type7_margin, "The same at 11 host cycles: at least 90 ticks (4.5 us) of AN004's 200 to spare. On paper: 93 ticks for the loop from a byte's arrival, with one refresh (DOC, SW p.32; INF, profile section 6), and 12 for the limit (design choice, profile 8a)") {
    CHECK_EQ(bytes_kept_at_pace(11), 256);
    int fastest = 20;
    while (fastest > 1 && bytes_kept_at_pace(fastest - 1) == 256) fastest--;
    // The project's hardware notes record 8 host cycles; only a different figure is printed.
    if (fastest != 8) printf("  type 7 keeps every byte down to %d host cycles a byte: %d ticks of AN004's 200 to spare\n", fastest, 200 - 10 * fastest);
}

TEST(tube_held_only_for_the_real_cost, "From RAM, in batches of 1, a Tube poll runs behind by no more than its own price and one batch: 30 ticks for fetch, Tube cycle, last cycle and a refresh, and 10 (DOC, SW p.32; INF, profile section 6; design choice, profile 8a)") {
    TubeRig rig(rom_with({spin(0)}));
    start_in_ram(rig, LOOP, {ldr_imm(LOAD | UP | PRE | BYTE, WORK, BASE, 0),
                             dp_imm(TST, true, 0, WORK, 0x80),
                             if_cond(0, branch(LOOP + 8, LOOP)),
                             spin(LOOP + 12)});
    rig.r(BASE) = TUBE;
    rig.run(500, 1);
    rig.ula.longest_late = 0;
    rig.ula.accesses = 0;
    rig.ula.early = 0;
    rig.run(20000, 1);
    CHECK_EQ(rig.ula.accesses > 500, true);
    CHECK_EQ(rig.ula.early, 0);
    CHECK_EQ(rig.ula.longest_late <= 40, true);
}

TEST(tube_board_timing_label, "The clock label is 10 ticks a host cycle and 3 ticks a cycle: 6.67 MHz, the 150 ns S cycle (DOC, SW p.33; INF, arithmetic)") {
    TubeUla ula;
    AesOptions o;
    AesTube tube(o, ula);
    const beebium::BoardTiming t = tube.board_timing();
    CHECK_EQ(t.ticks_per_host_cycle.numerator, 10);
    CHECK_EQ(t.ticks_per_host_cycle.denominator, 1);
    CHECK_EQ(beebium::nominal_clock_hz(t), 6666666);
}

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
// test_aes_board.cpp, tests the core and board together
// version 2.0.0, 20 November 2026.

// The same small program, ROM to end, under every RAM size and all three abort settings.

#include "../AesMemory.hpp"
#include "ArmTest.hpp"

static const uint32_t ROM = 0x3000000, START = ROM + 0x40, HANDLER = ROM + 0x400;
static const uint32_t PATTERN = 0x00FF00FF;

using Abort = AesOptions::MachineAbort;

struct NoTube : TubePort {
    int reads = 0;
    uint8_t read(int) override {
        reads++;
        return 0x40;
    }
    void write(int, uint8_t) override {}
};

static uint32_t mov_imm(int rd, uint32_t value8, int rot) { return dp_imm(MOV, false, rd, 0, value8, rot); }

// R3 fitted RAM, R5 at +1 MB, R7 at +2 MB, R9 at +4 MB, R11 region 10, R12 upper Tube
// half, R2 the Tube status. R13 is set by the abort handler. Addresses are rotated
// immediates (a rot field of n rotates right by 2n, DOC, SW p.15), so the program needs no data.
// With `user`, it runs in user mode, from a TEQP that leaves SVC (DOC, SW p.3).
static std::vector<uint8_t> program(bool user) {
    std::vector<uint32_t> at_start = {
        user ? dp_imm(TEQ, true, PC, PC, 0) : nop(),
        mov_imm(13, 0, 0),
        mov_imm(0, 0xFF, 0),
        dp_imm(ORR, false, 0, 0, 0xFF, 8),
        mov_imm(1, 0x01, 10),
        ldr_imm(UP | PRE, 0, 1, 0),
        ldr_imm(LOAD | UP | PRE, 3, 1, 0),
        mov_imm(4, 0x01, 6),
        ldr_imm(UP | PRE, 0, 4, 0),
        ldr_imm(LOAD | UP | PRE, 5, 4, 0),
        mov_imm(6, 0x02, 6),
        ldr_imm(UP | PRE, 0, 6, 0),
        ldr_imm(LOAD | UP | PRE, 7, 6, 0),
        mov_imm(8, 0x01, 5),
        dp_imm(ORR, false, 8, 8, 0x01, 10),
        ldr_imm(LOAD | UP | PRE, 9, 8, 0),
        mov_imm(10, 0x02, 4),
        ldr_imm(LOAD | UP | PRE, 11, 10, 0),
        mov_imm(10, 0x18, 6),
        ldr_imm(LOAD | UP | PRE, 12, 10, 0),
        mov_imm(10, 0x01, 4),
        ldr_imm(LOAD | UP | PRE | BYTE, 2, 10, 0),
    };
    at_start.push_back(branch(START + 4 * at_start.size(), START + 4 * at_start.size()));

    std::vector<uint8_t> rom(0x4000);
    const auto put = [&](uint32_t offset, uint32_t word) {
        for (int i = 0; i < 4; i++) rom[offset + i] = uint8_t(word >> (8 * i));
    };
    put(0, branch(0, START));
    for (size_t i = 0; i < at_start.size(); i++) put(0x40 + 4 * i, at_start[i]);
    put(0x400, dp_imm(ADD, false, 13, 13, 1));
    put(0x404, dp_imm(SUB, true, PC, 14, 4));
    return rom;
}

struct Result {
    uint32_t r2, r3, r5, r7, r9, r11, r12, r13;
    uint64_t ticks;
    int tube_reads;
};

static Result run_program(int ram_mb, AesOptions::MachineAbort abort_setting, bool user = false) {
    AesOptions o;
    o.ram = ram_mb;
    o.abort = abort_setting;
    AesTiming timing(o);
    NoTube tube;
    AesMemory mem(o, timing, tube);
    mem.set_rom(program(user));
    // The abort vector has to be in RAM before the run; reset puts the latch back.
    mem.write_word(0x10, branch(0x10, HANDLER), false);
    mem.reset();
    ArmCore cpu(mem, o.cpu);
    for (int i = 0; i < 80; i++) cpu.step();
    // R13 is the handler's count, so it is SVC's (DOC, SW p.5: R13 is banked).
    const auto r = [&](int n) { return cpu.reg_in_mode(cpu.r15() & 3, n); };
    return {r(2), r(3), r(5), r(7), r(9), r(11), r(12), cpu.reg_in_mode(3, 13), timing.ticks(), tube.reads};
}

TEST(board_ram_sizes, "Small program, each RAM size, abort off: fitted banks hold data, others read &FFFFFFFF (USER, Plugin option 3; INF, profile ram.repeat, ram.absent_bank)") {
    for (int mb : {2, 4}) {
        const Result r = run_program(mb, Abort::Off);
        CHECK_EQ(r.r3, PATTERN);
        CHECK_EQ(r.r5, mb >= 2 ? PATTERN : 0xFFFFFFFF);
        CHECK_EQ(r.r7, mb == 4 ? PATTERN : 0xFFFFFFFF);
        CHECK_EQ(r.r9, PATTERN);
        CHECK_EQ(r.r11, 0xFFFFFFFF);
        CHECK_EQ(r.r12, 0xFFFFFFFF);
        CHECK_EQ(r.r2, 0x40);
        CHECK_EQ(r.r13, 0);
    }
}

TEST(board_strict, "Small program in user mode, each RAM size, abort strict: fitted RAM works; every probe outside it aborts, stores and loads alike, and repeats hold no data (USER, 31 Oct 2026; user mode, USER, 4 Nov 2026)") {
    for (int mb : {2, 4}) {
        const Result r = run_program(mb, Abort::Strict, true);
        CHECK_EQ(r.r3, PATTERN);
        CHECK_EQ(r.r5, mb >= 2 ? PATTERN : 0);
        CHECK_EQ(r.r7, mb == 4 ? PATTERN : 0);
        CHECK_EQ(r.r9, 0);
        CHECK_EQ(r.r11, 0);
        CHECK_EQ(r.r12, 0);
        CHECK_EQ(r.r2, 0x40);
        CHECK_EQ(r.r13, mb == 4 ? 3 : (mb == 2 ? 5 : 7));
    }
}

TEST(board_strict_privileged, "Small program left in SVC, each RAM size, abort strict: nothing aborts and every value is as with abort off, because checking waits for user mode (USER, 4 Nov 2026)") {
    for (int mb : {2, 4}) {
        const Result on = run_program(mb, Abort::Strict);
        const Result off = run_program(mb, Abort::Off);
        CHECK_EQ(on.r13, 0);
        CHECK_EQ(on.r3, off.r3);
        CHECK_EQ(on.r5, off.r5);
        CHECK_EQ(on.r7, off.r7);
        CHECK_EQ(on.r9, off.r9);
        CHECK_EQ(on.r11, off.r11);
        CHECK_EQ(on.r12, off.r12);
        CHECK_EQ(on.r2, off.r2);
    }
}

TEST(board_time_counts, "Small program: time passes, and the RAM size alone does not change it (INF, time.ram_bank none)") {
    const Result a = run_program(2, Abort::Off), b = run_program(4, Abort::Off);
    CHECK_EQ(a.ticks > 0, true);
    CHECK_EQ(a.ticks, b.ticks);
}

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
// test_aes_exceptions.cpp, tests exceptions on the real memory map
// version 2.0.0, 20 November 2026.

// Each exception on the whole board with the program's own handlers: real memory map,
// no Executive, no Tube. The rig raises IRQ and FIQ itself. Spec: notes/test-specs.md, part 3.

#include "AesBoardTest.hpp"

using Abort = AesOptions::MachineAbort;

static const uint32_t PROG = 0x1000, HANDLER = 0x2000, FAR = 0x8000;
static const uint32_t EMPTY = TUBE + 0x800000;  // Outside abort strict's map (USER, 31 Oct 2026).
static const uint32_t OUTSIDE = 0x4000000;      // First address above 64 MB (DOC, SW 3.2).
static const uint32_t C_FLAG = 1u << 29, I_BIT = 1u << 27, F_BIT = 1u << 26;
static const uint32_t SVC_MODE = 3, IRQ_MODE = 2, FIQ_MODE = 1;
static const uint32_t PC_MASK = 0x3FFFFFC;

static const uint32_t UNDEFINED = AL | (0xCu << 24);  // DOC, SW p.36.
static const uint32_t RETURN_S = dp_reg(MOV, true, PC, 0, 14);
static const uint32_t RETURN_4 = dp_imm(SUB, true, PC, 14, 4);
static const uint32_t RETURN_8 = dp_imm(SUB, true, PC, 14, 8);
static const uint32_t COUNT = dp_imm(ADD, false, 9, 9, 1);

// abort strict checks from the first access in user mode, and each program here starts in
// user mode, so its accesses are checked (USER, 4 Nov 2026).
// R9 counts handler entries: no mode banks it (DOC, SW p.5). The latch is cleared first,
// so the vectors are read from RAM.
struct OwnBoard : Board {
    ArmCore cpu;
    explicit OwnBoard(AesOptions options = AesOptions()) : Board(options), cpu(mem, o.cpu) { clear_latch(); }

    void put(uint32_t addr, std::initializer_list<uint32_t> words) {
        for (uint32_t w : words) {
            mem.write_word(addr, w, false);
            addr += 4;
        }
    }
    void handler(uint32_t vector, uint32_t return_word) {
        put(vector, {branch(vector, HANDLER)});
        put(HANDLER, {COUNT, return_word});
    }
    void start(uint32_t r15) { cpu.set_r15(r15); }
    uint32_t& r(int n) { return cpu.reg_in_mode(0, n); }
    uint32_t r14(int mode) { return cpu.reg_in_mode(mode, 14); }
    uint32_t next_pc() const { return (cpu.r15() & PC_MASK) - 8; }
    uint32_t mode() const { return cpu.r15() & 3; }
    void step(int count) {
        for (int i = 0; i < count; i++) cpu.step();
    }
};

static AesOptions abort_on() {
    AesOptions o;
    o.abort = Abort::Strict;
    return o;
}

TEST(board_swi_handler, "SWI, own handler on the board: vector &08 read from RAM, SVC, I set, R14 next instruction, MOVS PC,R14 returns with the flags (DOC, SW 3.7)") {
    OwnBoard b;
    b.handler(0x08, RETURN_S);
    b.put(PROG, {swi(0x123), nop()});
    b.start(C_FLAG | PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x08);
    CHECK_EQ(b.mode(), SVC_MODE);
    CHECK_EQ(b.cpu.r15() & I_BIT, I_BIT);
    CHECK_EQ(b.r14(SVC_MODE), C_FLAG | (PROG + 4));
    b.step(3);
    CHECK_EQ(b.next_pc(), PROG + 4);
    CHECK_EQ(b.mode(), 0);
    CHECK_EQ(b.cpu.r15() & (C_FLAG | I_BIT), C_FLAG);
    CHECK_EQ(b.r(9), 1);
}

TEST(board_vector_from_rom, "SWI before the first write: the vector comes from the ROM, and from RAM once the latch clears (DOC, HW pp.34-35)") {
    std::vector<uint32_t> words(0x100 / 4, nop());
    words[0x08 / 4] = branch(0x08, ROM + 0x80);
    words[0x40 / 4] = swi();
    words[0x44 / 4] = nop();
    std::vector<uint8_t> image(0x4000);
    for (size_t i = 0; i < words.size(); i++) {
        for (int n = 0; n < 4; n++) image[4 * i + n] = uint8_t(words[i] >> (8 * n));
    }
    OwnBoard b;
    b.put(0x08, {branch(0x08, HANDLER)});
    b.mem.set_rom(image);
    b.mem.reset();
    CHECK_EQ(b.mem.boot_latch_set(), true);
    b.start(SVC_MODE | I_BIT | F_BIT | (ROM + 0x40));
    b.step(2);
    CHECK_EQ(b.next_pc(), ROM + 0x80);
    b.clear_latch();
    b.start(SVC_MODE | I_BIT | F_BIT | (ROM + 0x40));
    b.step(2);
    CHECK_EQ(b.next_pc(), HANDLER);
}

TEST(board_undef_handler, "Undefined, own handler on the board: vector &04, SVC, R14 instruction + 4, MOVS PC,R14 goes on at the next (DOC, SW 3.6, p.36)") {
    OwnBoard b;
    b.handler(0x04, RETURN_S);
    b.put(PROG, {UNDEFINED, nop()});
    b.start(PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x04);
    CHECK_EQ(b.mode(), SVC_MODE);
    CHECK_EQ(b.r14(SVC_MODE), PROG + 4);
    b.step(3);
    CHECK_EQ(b.next_pc(), PROG + 4);
    CHECK_EQ(b.r(9), 1);
}

TEST(board_adx_handler, "Address exception, own handler on the board: vector &14, SVC, R14 instruction + 8, load not done, SUBS PC,R14,#4 goes on at the next (DOC, SW 3.2)") {
    OwnBoard b;
    b.handler(0x14, RETURN_4);
    b.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0), nop()});
    b.r(1) = 0x55;
    b.r(2) = OUTSIDE;
    b.start(PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x14);
    CHECK_EQ(b.mode(), SVC_MODE);
    CHECK_EQ(b.cpu.r15() & I_BIT, I_BIT);
    CHECK_EQ(b.r14(SVC_MODE), PROG + 8);
    CHECK_EQ(b.r(1), 0x55);
    b.step(3);
    CHECK_EQ(b.next_pc(), PROG + 4);
    CHECK_EQ(b.r(9), 1);
}

TEST(board_adx_store, "Address exception on a store: nothing reaches RAM or the Tube at the low 26 bits (DS87 5.3.3, adapted)") {
    OwnBoard b;
    b.handler(0x14, RETURN_4);
    b.put(FAR, {0x11111111});
    b.put(PROG, {ldr_imm(UP | PRE, 1, 2, 0), ldr_imm(UP | PRE | BYTE, 1, 3, 4), nop()});
    b.r(1) = 0x55;
    b.r(2) = OUTSIDE + FAR;
    b.r(3) = OUTSIDE + TUBE;
    b.start(PROG);
    b.step(4);
    CHECK_EQ(b.next_pc(), PROG + 4);
    b.step(4);
    CHECK_EQ(b.next_pc(), PROG + 8);
    CHECK_EQ(b.mem.read_word(FAR, false), 0x11111111);
    CHECK_EQ(b.tube.writes, 0);
    CHECK_EQ(b.r(9), 2);
}

TEST(board_adx_ignores_abort, "abort strict: an address exception ignores ABORT, so vector &14, not &10, even where the low 26 bits are outside the map (DS87 5.3.3, adapted)") {
    OwnBoard b(abort_on());
    b.handler(0x14, RETURN_4);
    b.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0)});
    b.r(2) = OUTSIDE + EMPTY;
    b.start(PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x14);
}

TEST(board_data_abort_load, "abort strict: load from an address outside the map, vector &10, SVC, R14 instruction + 8, no load, no writeback; subtract 8 runs it again (DOC, SW 3.3)") {
    OwnBoard b(abort_on());
    b.handler(0x10, RETURN_8);
    b.put(FAR + 4, {0x600D});
    b.put(HANDLER + 4, {dp_imm(MOV, false, 2, 0, FAR >> 12, 10), RETURN_8});
    b.put(PROG, {ldr_imm(LOAD | UP | PRE | WB, 1, 2, 4), nop()});
    b.r(1) = 0x55;
    b.r(2) = EMPTY;
    b.start(PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x10);
    CHECK_EQ(b.mode(), SVC_MODE);
    CHECK_EQ(b.cpu.r15() & I_BIT, I_BIT);
    CHECK_EQ(b.r14(SVC_MODE), PROG + 8);
    CHECK_EQ(b.r(1), 0x55);
    CHECK_EQ(b.r(2), EMPTY);
    b.step(4);
    CHECK_EQ(b.next_pc(), PROG);
    b.step(1);
    CHECK_EQ(b.r(1), 0x600D);
    CHECK_EQ(b.r(2), FAR + 4);
    CHECK_EQ(b.r(9), 1);
}

TEST(board_data_abort_store, "abort strict: store to an address outside the map, vector &10, nothing stored, no writeback (DOC, SW 3.3)") {
    OwnBoard b(abort_on());
    b.handler(0x10, RETURN_S);
    b.put(PROG, {ldr_imm(UP | PRE | WB, 1, 2, 4)});
    b.r(2) = EMPTY;
    b.start(PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x10);
    CHECK_EQ(b.r14(SVC_MODE), PROG + 8);
    CHECK_EQ(b.r(2), EMPTY);
    CHECK_EQ(b.tube.writes, 0);
}

TEST(board_prefetch_abort, "abort strict: a jump to an address outside the map, vector &0C when it reaches execution, SVC, R14 that address + 4 (DOC, SW 3.3)") {
    OwnBoard b(abort_on());
    b.handler(0x0C, RETURN_S);
    b.put(PROG, {dp_reg(MOV, false, PC, 0, 2)});
    b.r(2) = EMPTY;
    b.start(PROG);
    b.step(1);
    CHECK_EQ(b.next_pc(), EMPTY);
    CHECK_EQ(b.r(9), 0);
    b.step(1);
    CHECK_EQ(b.next_pc(), 0x0C);
    CHECK_EQ(b.mode(), SVC_MODE);
    CHECK_EQ(b.cpu.r15() & I_BIT, I_BIT);
    CHECK_EQ(b.r14(SVC_MODE), EMPTY + 4);
}

TEST(board_abort_default_never, "abort off, the default: the same load, store and jump take no abort vector (USER, Plugin option 2; DOC low-res, ABORT tied low)") {
    OwnBoard b;
    b.handler(0x0C, RETURN_S);
    b.handler(0x10, RETURN_S);
    b.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0), ldr_imm(UP | PRE, 1, 2, 0), dp_reg(MOV, false, PC, 0, 2)});
    b.r(1) = 0x55;
    b.r(2) = EMPTY;
    b.start(PROG);
    b.step(2);
    CHECK_EQ(b.next_pc(), PROG + 8);
    CHECK_EQ(b.mode(), 0);
    b.step(4);
    CHECK_EQ(b.mode(), 0);
    CHECK_EQ(b.r(9), 0);
}

TEST(board_irq_handler, "IRQ on the board, line raised by the rig: vector &18, IRQ mode, I set, F kept, SUBS PC,R14,#4 resumes the instruction it interrupted (DOC, SW 3.5)") {
    OwnBoard b;
    b.handler(0x18, RETURN_4);
    b.put(PROG, {dp_imm(ADD, false, 3, 3, 1), dp_imm(ADD, false, 3, 3, 1), nop()});
    b.start(PROG);
    b.step(1);
    b.cpu.set_irq(true);
    b.step(1);
    b.cpu.set_irq(false);
    CHECK_EQ(b.next_pc(), 0x18);
    CHECK_EQ(b.mode(), IRQ_MODE);
    CHECK_EQ(b.cpu.r15() & (I_BIT | F_BIT), I_BIT);
    CHECK_EQ(b.r14(IRQ_MODE), PROG + 8);
    CHECK_EQ(b.r(3), 1);
    b.step(3);
    CHECK_EQ(b.next_pc(), PROG + 4);
    CHECK_EQ(b.mode(), 0);
    b.step(1);
    CHECK_EQ(b.r(3), 2);
    CHECK_EQ(b.r(9), 1);
}

TEST(board_fiq_handler, "FIQ on the board, line raised by the rig: routine at &1C itself, FIQ mode, I and F set, beats IRQ, then IRQ if still held (DOC, SW 3.4, 3.5, pp.27, 31)") {
    OwnBoard b;
    b.put(0x1C, {COUNT, RETURN_4});
    b.handler(0x18, RETURN_4);
    b.put(PROG, {nop(), nop(), nop()});
    b.start(PROG);
    b.cpu.set_irq(true);
    b.cpu.set_fiq(true);
    b.step(1);
    b.cpu.set_fiq(false);
    CHECK_EQ(b.next_pc(), 0x1C);
    CHECK_EQ(b.mode(), FIQ_MODE);
    CHECK_EQ(b.cpu.r15() & (I_BIT | F_BIT), I_BIT | F_BIT);
    CHECK_EQ(b.r14(FIQ_MODE), PROG + 4);
    b.step(2);
    CHECK_EQ(b.r(9), 1);
    CHECK_EQ(b.next_pc(), PROG);
    b.step(1);
    b.cpu.set_irq(false);
    CHECK_EQ(b.next_pc(), 0x18);
    CHECK_EQ(b.mode(), IRQ_MODE);
}

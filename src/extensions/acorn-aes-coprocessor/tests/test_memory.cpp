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
// test_memory.cpp, tests LDR, STR, LDM and STM
// version 2.0.0, 20 November 2026.

// LDR, STR, LDM and STM.

#include "ArmTest.hpp"

using O = ArmOptions;
static const uint32_t N = 1u << 31, V = 1u << 28, I = 1u << 27;
static const uint32_t A = 0x1000, D = 0x8000;

TEST(ldr_pre_post, "Four addressing forms and writeback (DOC, SW pp.20-21)") {
    Rig k;
    k.bus.poke(D + 8, 0x11111111);
    k.bus.poke(D, 0x22222222);
    k.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 8),
               ldr_imm(LOAD | PRE | UP | WB, 2, 1, 8),
               ldr_imm(LOAD, 3, 1, 8),
               ldr_imm(LOAD | UP, 4, 1, 8)});
    k.start(A);
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.r(0), 0x11111111);
    CHECK_EQ(k.r(1), D);
    k.step();
    CHECK_EQ(k.r(2), 0x11111111);
    CHECK_EQ(k.r(1), D + 8);
    k.step();
    CHECK_EQ(k.r(3), 0x11111111);
    CHECK_EQ(k.r(1), D);
    k.step();
    CHECK_EQ(k.r(4), 0x22222222);
    CHECK_EQ(k.r(1), D + 8);
}

TEST(ldr_reg_offset, "Register offset, shifted, plus or minus (DOC, SW p.21)") {
    Rig k;
    k.bus.poke(D + 16, 0x33333333);
    k.bus.poke(D - 16, 0x44444444);
    k.load(A, {ldr_reg(LOAD | PRE | UP, 0, 1, 2, LSL, 2), ldr_reg(LOAD | PRE, 3, 1, 2, LSL, 2)});
    k.start(A);
    k.r(1) = D;
    k.r(2) = 4;
    k.step(2);
    CHECK_EQ(k.r(0), 0x33333333);
    CHECK_EQ(k.r(3), 0x44444444);
}

TEST(byte_access, "Byte load is zero extended; store changes one byte (DOC, SW p.19)") {
    Rig k;
    k.bus.poke(D, 0x44332211);
    k.load(A, {ldr_imm(LOAD | PRE | UP | BYTE, 0, 1, 2),
               ldr_imm(PRE | UP | BYTE, 2, 1, 1)});
    k.start(A);
    k.r(1) = D;
    k.r(2) = 0xFFFFFFAB;
    k.step(2);
    CHECK_EQ(k.r(0), 0x33);
    CHECK_EQ(k.bus.peek(D), 0x4433AB11);
}

TEST(str_word, "STR stores the word; a base update comes with it (DOC, SW pp.20-21)") {
    Rig k;
    k.load(A, {ldr_imm(PRE | UP | WB, 0, 1, 4), ldr_imm(UP, 0, 1, 8)});
    k.start(A);
    k.r(0) = 0x12345678;
    k.r(1) = D;
    k.step(2);
    CHECK_EQ(k.bus.peek(D + 4), 0x12345678);
    CHECK_EQ(k.bus.peek(D + 12), 0);
    CHECK_EQ(k.r(1), D + 12);
}

TEST(ldr_base_is_rd, "LDR with Rd = Rn and writeback: the load wins (DS87 9.4, adapted)") {
    Rig k;
    k.bus.poke(D, 0x55555555);
    k.load(A, {ldr_imm(LOAD | UP, 1, 1, 4)});
    k.start(A);
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.r(1), 0x55555555);
}

TEST(str_base_is_rd, "STR with Rd = Rn and writeback stores the old base (DS87 9.5, adapted)") {
    Rig k;
    k.load(A, {ldr_imm(UP, 1, 1, 4)});
    k.start(A);
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.bus.peek(D), D);
    CHECK_EQ(k.r(1), D + 4);
}

TEST(ldr_pc_base, "R15 as base is PC+8 without PSR (DOC, SW p.21; DS87 6.5.4, adapted)") {
    Rig k;
    k.bus.poke(A + 8 + 4, 0x66666666);
    k.load(A, {ldr_imm(LOAD | PRE | UP, 0, PC, 4)});
    k.start(N | V | A);
    k.step();
    CHECK_EQ(k.r(0), 0x66666666);
}

TEST(ldr_to_pc, "LDR into R15 changes PC only, never the PSR (DOC, SW p.21)") {
    Rig k;
    k.bus.poke(D, 0xF0002003);
    k.load(A, {ldr_imm(LOAD | PRE | UP, PC, 1, 0)});
    k.start(V | A);
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), V);
}

TEST(str_pc_value, "STR of R15: PC+12 with PSR (DS87 6.5.4, adapted; the real AES, test 10 C5)") {
    Rig k;
    k.load(A, {ldr_imm(PRE | UP, PC, 1, 0)});
    k.start(N | A);
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.bus.peek(D), N | (A + 12));
}

TEST(pc_base_writeback, "Writeback to R15 as base moves the PC only (DS87 9.4, adapted)") {
    Rig k;
    k.load(A, {ldr_imm(LOAD | PRE | UP | WB, 0, PC, 0x100)});
    k.start(V | A);
    k.step();
    CHECK_EQ(k.pc(), A + 8 + 0x100 + 8);
    CHECK_EQ(k.psr(), V);
}

TEST(q4_ldr_rs, "Q4: a register-shifted offset exists (DOC, DS13, SW85 p.21; the real AES, test 5 Q4)") {
    Rig k;
    k.bus.poke(D + 16, 0x77777777);
    k.load(A, {ldr_rs(LOAD | PRE | UP, 0, 1, 2, LSL, 3)});
    k.start(A);
    k.r(1) = D;
    k.r(2) = 1;
    k.r(3) = 4;
    Cost c = k.cost_of_next_instruction();
    CHECK_EQ(k.r(0), 0x77777777);
    CHECK_EQ(c.s, 3);
    CHECK_EQ(c.n, 1);
}

TEST(time_ldr_rs, "time.ldr_reg_shift: 1S extra (DOC, SW85 p.21); none alternative") {
    O o;
    o.time_ldr_reg_shift = O::LdrRegShiftTime::None;
    Rig m(o);
    m.load(A, {ldr_rs(LOAD | PRE | UP, 0, 1, 2, LSL, 3)});
    m.start(A);
    m.r(1) = D;
    Cost c = m.cost_of_next_instruction();
    CHECK_EQ(c.s, 2);
    CHECK_EQ(c.n, 1);
}

TEST(unaligned, "cpu.word_unaligned: low bits ignored (UNK); Rotate alternative (DS87 6.5.3)") {
    Rig k;
    k.bus.poke(D, 0x44332211);
    k.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 1), ldr_imm(PRE | UP, 2, 1, 1)});
    k.start(A);
    k.r(1) = D;
    k.r(2) = 0xAABBCCDD;
    k.step();
    CHECK_EQ(k.r(0), 0x44332211);
    k.step();
    CHECK_EQ(k.bus.peek(D), 0xAABBCCDD);
    O o;
    o.word_unaligned = O::WordUnaligned::Rotate;
    Rig m(o);
    m.bus.poke(D, 0x44332211);
    m.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 1)});
    m.start(A);
    m.r(1) = D;
    m.step();
    CHECK_EQ(m.r(0), 0x11443322);
}

TEST(ldr_rs_bit7, "Guess, 7a: bit 7 on a register-shifted offset follows cpu.enc_a_bit7 (INF, SW p.19)") {
    const uint32_t word = ldr_rs(LOAD | PRE | UP, 0, 1, 2, LSL, 3) | 0x80;
    Rig k;
    k.bus.poke(D + 16, 0x77777777);
    k.load(A, {word});
    k.start(A);
    k.r(1) = D;
    k.r(2) = 1;
    k.r(3) = 4;
    k.step();
    CHECK_EQ(k.r(0), 0x77777777);
    O o;
    o.enc_a_bit7 = O::EncABit7::Traps;
    Rig m(o);
    m.load(A, {word});
    m.start(A);
    m.step();
    CHECK_EQ(m.pc(), 4 + 8);
}

TEST(ldr_rs_r15, "Guess, 7a: R15 in a register-shifted LDR reads +12, as with a data-processing register shift (INF, DS87 6.3.6, adapted)") {
    for (int ahead : {12}) {
        Rig k;
        k.bus.poke(A + ahead, 0x12341234);
        k.load(A, {ldr_rs(LOAD | PRE | UP, 0, PC, 1, LSL, 2)});
        k.start(A);
        k.r(1) = 0;
        k.r(2) = 0;
        k.step();
        CHECK_EQ(k.r(0), 0x12341234);
        Rig m;
        m.bus.poke(D + A + ahead, 0x56785678);
        m.load(A, {ldr_rs(LOAD | PRE | UP, 0, 1, PC, LSL, 2)});
        m.start(A);
        m.r(1) = D;
        m.r(2) = 0;
        m.step();
        CHECK_EQ(m.r(0), 0x56785678);
    }
}

TEST(str_rs_guesses, "Guess, 7a: STR with a register-shifted offset: R15 and bit 7 as for LDR (INF, SW p.19; DS87 6.3.6, adapted)") {
    for (int ahead : {12}) {
        Rig k;
        k.load(A, {ldr_rs(PRE | UP, 0, PC, 1, LSL, 2)});
        k.start(A);
        k.r(0) = 0x12341234;
        k.r(1) = 0;
        k.r(2) = 0;
        k.step();
        CHECK_EQ(k.bus.peek(A + ahead), 0x12341234);
    }
    for (bool traps : {false, true}) {
        O o;
        if (traps) o.enc_a_bit7 = O::EncABit7::Traps;
        Rig k(o);
        k.load(A, {ldr_rs(PRE | UP, 0, 1, 3, LSL, 2) | (1u << 7)});
        k.start(A);
        k.r(0) = 0x56785678;
        k.r(1) = D;
        k.r(2) = 0;
        k.r(3) = 0;
        k.step();
        CHECK_EQ(k.bus.peek(D), traps ? 0 : 0x56785678);
    }
}

// R1, R5, R7 with Rn = &1000 and writeback, to compare with the figures in DS87 6.6.2.
TEST(block_four_modes, "LDM/STM addressing, lowest register lowest (DOC, SW p.23; DS87 6.6.2)") {
    struct {
        uint32_t flags, first, end;
    } t[] = {{UP, 0x1000, 0x100C},
             {UP | PRE, 0x1004, 0x100C},
             {0, 0x0FF8, 0x0FF4},
             {PRE, 0x0FF4, 0x0FF4}};
    const uint32_t list = (1u << 1) | (1u << 5) | (1u << 7);
    for (auto& e : t) {
        Rig k;
        k.load(A + 0x1000, {block(e.flags | WB, 2, list)});
        k.start(A + 0x1000);
        k.r(1) = 0xA1;
        k.r(5) = 0xA5;
        k.r(7) = 0xA7;
        k.r(2) = 0x1000;
        k.step();
        CHECK_EQ(k.bus.peek(e.first), 0xA1);
        CHECK_EQ(k.bus.peek(e.first + 4), 0xA5);
        CHECK_EQ(k.bus.peek(e.first + 8), 0xA7);
        CHECK_EQ(k.r(2), e.end);

        Rig m;
        m.load(A + 0x1000, {block(e.flags | WB | LOAD, 2, list)});
        m.bus.poke(e.first, 0xB1);
        m.bus.poke(e.first + 4, 0xB5);
        m.bus.poke(e.first + 8, 0xB7);
        m.start(A + 0x1000);
        m.r(2) = 0x1000;
        m.step();
        CHECK_EQ(m.r(1), 0xB1);
        CHECK_EQ(m.r(5), 0xB5);
        CHECK_EQ(m.r(7), 0xB7);
        CHECK_EQ(m.r(2), e.end);
    }
}

TEST(block_no_wb, "Without writeback the base stays (DOC, SW p.24; DS87 6.6.2)") {
    Rig k;
    k.load(A, {block(UP, 2, 3)});
    k.start(A);
    k.r(2) = D;
    k.step();
    CHECK_EQ(k.r(2), D);
}

TEST(stm_base_in_list, "STM: base first stores old value, later stores new (DOC, SW85 p.15)") {
    Rig k;
    k.load(A, {block(UP | WB, 2, (1u << 2) | (1u << 3))});
    k.start(A);
    k.r(2) = D;
    k.r(3) = 9;
    k.step();
    CHECK_EQ(k.bus.peek(D), D);
    Rig m;
    m.load(A, {block(UP | WB, 2, (1u << 1) | (1u << 2))});
    m.start(A);
    m.r(2) = D;
    m.step();
    CHECK_EQ(m.bus.peek(D + 4), D + 8);
}

TEST(ldm_base_in_list, "LDM always overwrites the updated base (DOC, SW85 p.15)") {
    Rig k;
    k.bus.poke(D, 0xCAFE);
    k.load(A, {block(UP | WB | LOAD, 2, 1u << 2)});
    k.start(A);
    k.r(2) = D;
    k.step();
    CHECK_EQ(k.r(2), 0xCAFE);
}

TEST(stm_pc_value, "STM of R15: PC+12 with PSR (DS87 6.6.3, adapted; the real AES, test 10 C6, C7)") {
    for (int v : {12}) {
        Rig k;
        k.load(A, {block(UP, 2, (1u << 0) | (1u << 15))});
        k.start(N | A);
        k.r(2) = D;
        k.step();
        CHECK_EQ(k.bus.peek(D + 4), N | (A + v));
    }
}

TEST(ldm_pc_no_s, "LDM R15 without S: PC only (DOC, SW p.24)") {
    Rig k;
    k.bus.poke(D, 0xF0002003);
    k.load(A, {block(UP | LOAD, 2, 1u << 15)});
    k.start(V | A | 3);
    k.r(2) = D;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), V | 3);
}

TEST(ldm_pc_s_user, "LDM R15 with S, user: flags only (DOC, SW85 p.15)") {
    Rig k;
    k.bus.poke(D, 0xFC002003);
    k.load(A, {block(UP | LOAD | PSRBIT, 2, 1u << 15)});
    k.start(A);
    k.r(2) = D;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), 0xF0000000);
}

TEST(ldm_pc_s_svc, "LDM R15 with S, non-user: all PSR bits, new mode (DOC, SW85 p.15)") {
    Rig k;
    k.bus.poke(D, 0x98002000);
    k.load(A, {block(UP | LOAD | PSRBIT, 2, 1u << 15)});
    k.start(A | 3);
    k.r(2) = D;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), N | V | I);
}

TEST(stm_user_bank, "STM with S, non-user: user bank, writeback to user base (DOC, SW85 p.16)") {
    Rig k;
    k.load(A, {block(UP | WB | PSRBIT, 13, (1u << 13) | (1u << 14))});
    k.start(A | 3);
    k.cpu->reg_in_mode(3, 13) = D;
    k.cpu->reg_in_mode(3, 14) = 0xBAD;
    k.cpu->reg_in_mode(0, 13) = 0x130;
    k.cpu->reg_in_mode(0, 14) = 0x140;
    k.step();
    CHECK_EQ(k.bus.peek(D), 0x130);
    CHECK_EQ(k.bus.peek(D + 4), 0x140);
    CHECK_EQ(k.cpu->reg_in_mode(3, 13), D);
    CHECK_EQ(k.cpu->reg_in_mode(0, 13), D + 8);
}

TEST(stm_s_user_mode, "STM with S in user mode: S ignored (DOC, SW85 p.16)") {
    Rig k;
    k.load(A, {block(UP | WB | PSRBIT, 2, 1u << 0)});
    k.start(A);
    k.r(0) = 0x11;
    k.r(2) = D;
    k.step();
    CHECK_EQ(k.bus.peek(D), 0x11);
    CHECK_EQ(k.r(2), D + 4);
}

TEST(q10_ldm_user, "Q10: cpu.ldm_user_bank, loads user bank (DS87 6.6.4, adapted); current alt") {
    for (bool user_bank : {true, false}) {
        O o;
        o.ldm_user_bank = user_bank;
        Rig k(o);
        k.bus.poke(D, 0x999);
        k.load(A, {block(UP | LOAD | PSRBIT, 2, 1u << 13)});
        k.start(A | 3);
        k.r(2) = D;
        k.cpu->reg_in_mode(3, 13) = 0x333;
        k.cpu->reg_in_mode(0, 13) = 0x111;
        k.step();
        CHECK_EQ(k.cpu->reg_in_mode(0, 13), user_bank ? 0x999 : 0x111);
        CHECK_EQ(k.cpu->reg_in_mode(3, 13), user_bank ? 0x333 : 0x999);
    }
}

TEST(q6_ldm_pc_wb, "Q6: cpu.ldm_pc_wb, LDMFD R13!,{PC}^ writes back to user R13 (INF, DS13)") {
    const uint32_t word = block(UP | WB | LOAD | PSRBIT, 13, 1u << 15);
    for (bool user_bank : {true, false}) {
        O o;
        o.ldm_pc_wb = user_bank ? O::LdmPcWb::UserBank : O::LdmPcWb::CurrentBank;
        Rig k(o);
        k.bus.poke(D, 0x2000);
        k.load(A, {word});
        k.start(A | 3);
        k.cpu->reg_in_mode(3, 13) = D;
        k.cpu->reg_in_mode(0, 13) = 0x111;
        k.step();
        CHECK_EQ(k.cpu->reg_in_mode(0, 13), user_bank ? D + 4 : 0x111);
        CHECK_EQ(k.cpu->reg_in_mode(3, 13), user_bank ? D : D + 4);
        CHECK_EQ(k.psr(), 0);
    }
}

TEST(q6_not_multi, "Q6 needs a single register: a longer list writes back normally (DOC, DS13)") {
    Rig k;
    k.bus.poke(D, 1);
    k.bus.poke(D + 4, 0x2000);
    k.load(A, {block(UP | WB | LOAD | PSRBIT, 13, (1u << 0) | (1u << 15))});
    k.start(A | 3);
    k.cpu->reg_in_mode(3, 13) = D;
    k.cpu->reg_in_mode(0, 13) = 0x111;
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(3, 13), D + 8);
    CHECK_EQ(k.cpu->reg_in_mode(0, 13), 0x111);
}

TEST(block_pc_base, "R15 as block base: PSR bits join the address, no writeback (DOC, SW p.24)") {
    Rig k;
    k.bus.poke(A + 8, 0xFEED);
    k.load(A, {block(UP | WB | LOAD, PC, 1u << 0), nop()});
    k.start(A);
    k.step();
    CHECK_EQ(k.r(0), 0xFEED);
    CHECK_EQ(k.pc(), A + 12);
    Rig m;
    m.load(A, {block(UP | LOAD, PC, 1u << 0)});
    m.start(V | A);
    m.step();
    CHECK_EQ(m.pc(), 0x14 + 8);
}

TEST(block_pc_base_user_bank, "R15 as base with the user-bank bit: no writeback (DOC, SW p.24 note 1)") {
    Rig k;
    k.load(A, {block(UP | WB | PSRBIT, PC, 1u << 0)});
    k.start(A | 3);
    k.r(0) = 0xFEED;
    k.step();
    CHECK_EQ(k.bus.peek(A + 8), 0xFEED);
    CHECK_EQ(k.pc(), A + 12);
}

TEST(empty_list, "Empty list: nothing transferred, no writeback. A guess: UNK (SW p.24 'undefined')") {
    Rig k;
    k.load(A, {block(UP | WB, 2, 0), block(UP | WB | LOAD, 2, 0)});
    k.start(A);
    k.r(2) = D;
    k.step(2);
    CHECK_EQ(k.r(2), D);
    CHECK_EQ(k.pc(), A + 16);
}

TEST(trans_ignored, "cpu.trans_ignored: post-indexed T bit acts as plain LDR (INF, DOC low-res)") {
    Rig k;
    k.bus.poke(D, 0x88);
    k.load(A, {ldr_imm(LOAD | UP | WB, 0, 1, 4)});
    k.start(A | 3);
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.r(0), 0x88);
    CHECK_EQ(k.r(1), D + 4);
}

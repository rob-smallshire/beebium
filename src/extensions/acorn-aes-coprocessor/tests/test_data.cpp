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
// test_data.cpp, tests data processing, shifts and conditions
// version 2.0.0, 20 November 2026.

// Data processing, shifter, condition codes and R15 as an operand.

#include "ArmTest.hpp"

using O = ArmOptions;
static const uint32_t N = 1u << 31, Z = 1u << 30, C = 1u << 29, V = 1u << 28;
static const uint32_t I = 1u << 27, F = 1u << 26;
static const uint32_t A = 0x1000;

// One helper because most shifter facts need the same set-up: R1 = rm, R2 = rs.
static uint32_t one(Rig& k, uint32_t word, uint32_t rm, uint32_t rs, uint32_t flags) {
    k.load(A, {word});
    k.start(flags | A);
    k.r(0) = 0xDEADBEEF;
    k.r(1) = rm;
    k.r(2) = rs;
    k.step();
    return k.r(0);
}

TEST(cond_table, "Condition codes, all 16 against all flags (DOC, SW p.8)") {
    for (uint32_t f = 0; f < 16; f++) {
        const bool n = f & 8, z = f & 4, c = f & 2, v = f & 1;
        const bool want[16] = {z, !z, c, !c, n, !n, v, !v, c && !z, !c || z, n == v, n != v,
                               n == v && !z, n != v || z, true, false};
        for (uint32_t cond = 0; cond < 16; cond++) {
            Rig k;
            k.load(A, {if_cond(cond, dp_imm(MOV, false, 0, 0, 1))});
            k.start((f << 28) | A);
            k.r(0) = 7;
            k.step();
            CHECK_EQ(k.r(0), want[cond] ? 1 : 7);
        }
    }
}

TEST(alu_ops, "Sixteen data-processing operations (DOC, SW p.12)") {
    const uint32_t a = 0xF0F01234, b = 0x0FF00111;
    struct {
        int op;
        uint32_t want;
    } t[] = {{AND, a & b},  {EOR, a ^ b},  {SUB, a - b},     {RSB, b - a},
             {ADD, a + b},  {ADC, a + b + 1}, {SBC, a - b},  {RSC, b - a},
             {ORR, a | b},  {MOV, b},      {BIC, a & ~b},    {MVN, ~b}};
    for (auto& e : t) {
        Rig k;
        k.load(A, {dp_reg(e.op, false, 0, 3, 1)});
        k.start(C | A);
        k.r(1) = b;
        k.r(3) = a;
        k.step();
        CHECK_EQ(k.r(0), e.want);
    }
    Rig k;
    k.load(A, {dp_reg(SBC, false, 0, 3, 1), dp_reg(RSC, false, 4, 3, 1)});
    k.start(A);
    k.r(1) = b;
    k.r(3) = a;
    k.step(2);
    CHECK_EQ(k.r(0), a - b - 1);
    CHECK_EQ(k.r(4), b - a - 1);
}

TEST(flags_arith, "Arithmetic sets N,Z,C,V from the ALU; C is not-borrow (DOC, SW pp.3, 12)") {
    struct {
        int op;
        uint32_t x, y, want;
    } t[] = {{ADD, 0xFFFFFFFF, 1, Z | C},
             {ADD, 0x7FFFFFFF, 1, N | V},
             {SUB, 5, 5, Z | C},
             {SUB, 4, 5, N},
             {SUB, 0x80000000, 1, C | V},
             {RSB, 5, 4, N},
             {CMP, 7, 7, Z | C},
             {CMN, 0xFFFFFFFF, 1, Z | C}};
    for (auto& e : t) {
        Rig k;
        k.load(A, {dp_reg(e.op, true, 0, 3, 1)});
        k.start(A);
        k.r(1) = e.y;
        k.r(3) = e.x;
        k.step();
        CHECK_EQ(k.psr(), e.want);
    }
}

TEST(flags_logical, "Logical ops: N,Z from ALU, C from shifter, V unaffected (DOC, SW p.12)") {
    Rig k;
    CHECK_EQ(one(k, dp_reg(ORR, true, 0, 3, 1, LSL, 1), 0x80000000, 0, V), 0);
    CHECK_EQ(k.psr(), Z | C | V);
    Rig m;
    one(m, dp_reg(MOV, true, 0, 0, 1), 0x80000000, 0, C);
    CHECK_EQ(m.psr(), N | C);
}

TEST(flags_s_clear, "S clear leaves the flags alone (DOC, SW p.17)") {
    Rig k;
    CHECK_EQ(one(k, dp_reg(ADD, false, 0, 1, 2), 0xFFFFFFFF, 1, 0), 0);
    CHECK_EQ(k.psr(), 0);
}

TEST(shift_imm, "Instruction shifts, with 0 meaning 32 for LSR and ASR (DOC, SW pp.13-15)") {
    struct {
        uint32_t ins, rm, in_flags, want, want_c;
    } t[] = {
        {dp_reg(MOV, true, 0, 0, 1, LSL, 0), 0x80000001, C, 0x80000001, C},
        {dp_reg(MOV, true, 0, 0, 1, LSL, 0), 0x80000001, 0, 0x80000001, 0},
        {dp_reg(MOV, true, 0, 0, 1, LSL, 4), 0x18000000, 0, 0x80000000, C},
        {dp_reg(MOV, true, 0, 0, 1, LSR, 4), 0x00000018, 0, 1, C},
        {dp_reg(MOV, true, 0, 0, 1, LSR, 0), 0x80000000, 0, 0, C},
        {dp_reg(MOV, true, 0, 0, 1, LSR, 0), 0x7FFFFFFF, C, 0, 0},
        {dp_reg(MOV, true, 0, 0, 1, ASR, 4), 0x80000018, 0, 0xF8000001, C},
        {dp_reg(MOV, true, 0, 0, 1, ASR, 0), 0x80000000, 0, 0xFFFFFFFF, C},
        {dp_reg(MOV, true, 0, 0, 1, ASR, 0), 0x7FFFFFFF, C, 0, 0},
        {dp_reg(MOV, true, 0, 0, 1, ROR, 4), 0x00000018, 0, 0x80000001, C},
    };
    for (auto& e : t) {
        Rig k;
        CHECK_EQ(one(k, e.ins, e.rm, 0, e.in_flags), e.want);
        CHECK_EQ(k.psr() & C, e.want_c);
    }
}

TEST(rrx_result, "RRX: carry goes to bit 31, then bit 0 to carry (DOC, SW p.15)") {
    Rig k;
    CHECK_EQ(one(k, dp_reg(MOV, true, 0, 0, 1, ROR, 0), 0x00000002, 0, C), 0x80000001);
    CHECK_EQ(k.psr() & C, 0);
    Rig m;
    CHECK_EQ(one(m, dp_reg(MOV, true, 0, 0, 1, ROR, 0), 0x00000001, 0, 0), 0);
    CHECK_EQ(m.psr() & C, C);
}

TEST(q1_rrx_carry, "Q1: RRX carry, bit 0 OR bit 31 (DOC, SW p.18, DS13; the real AES, test 5 Q1)") {
    for (uint32_t rm : {0x00000000u, 0x00000001u, 0x80000000u, 0x80000001u}) {
        for (uint32_t cin : {0u, C}) {
            Rig k;
            one(k, dp_reg(MOV, true, 0, 0, 1, ROR, 0), rm, 0, cin);
            CHECK_EQ(k.psr() & C, ((rm & 1) || (rm >> 31)) ? C : 0);
        }
    }
}

TEST(shift_reg, "Register shifts, amounts 1 to 255, low byte only (DOC, SW pp.14-15)") {
    struct {
        int type;
        uint32_t rm, rs, want, want_c;
    } t[] = {
        {LSL, 0x00000003, 4, 0x30, 0},
        {LSL, 0x10000001, 4, 0x10, C},
        {LSL, 0x00000001, 32, 0, C},
        {LSL, 0xFFFFFFFF, 33, 0, 0},
        {LSL, 0xFFFFFFFF, 255, 0, 0},
        {LSR, 0x00000018, 4, 1, C},
        {LSR, 0x80000000, 32, 0, C},
        {LSR, 0xFFFFFFFF, 33, 0, 0},
        {ASR, 0x80000018, 4, 0xF8000001, C},
        {ASR, 0x80000000, 32, 0xFFFFFFFF, C},
        {ASR, 0x80000000, 200, 0xFFFFFFFF, C},
        {ASR, 0x7FFFFFFF, 200, 0, 0},
        {LSL, 0x00000003, 0x10004, 0x30, 0},
    };
    for (auto& e : t) {
        Rig k;
        CHECK_EQ(one(k, dp_rs(MOV, true, 0, 0, 1, e.type, 2), e.rm, e.rs, 0), e.want);
        CHECK_EQ(k.psr() & C, e.want_c);
    }
}

TEST(reg_shift_zero, "Rs = 0 leaves value and carry (DOC, SW pp.14-15; the real AES, test 5 Q2)") {
    for (int type : {LSL, LSR, ASR, ROR}) {
        Rig k;
        CHECK_EQ(one(k, dp_rs(MOV, true, 0, 0, 1, type, 2), 0x80000001, 0x100, C), 0x80000001);
        CHECK_EQ(k.psr() & C, C);
    }
}

TEST(q2_ror_carry, "Q2: cpu.ror_reg_carry, the real AES's rule (USER, 15 Nov 2026) and three readings (DOC, SW p.18; DS13; DS87 6.3.3)") {
    const uint32_t rm = 0x80000081;  // bits 31, 7 and 0, as item 50 proposes; test 5 Q2's operand
    const O::RorRegCarry alt[] = {O::RorRegCarry::ZeroAbove31, O::RorRegCarry::ZeroAtMultiplesOf32,
                                  O::RorRegCarry::Arm2};
    struct {
        uint32_t amount, result, aes, alt[3];  // alt: SW p.18, DS13, ARM2
    } t[] = {
        {1, 0xC0000040, C, {C, C, C}},
        {31, 0x00000103, 0, {0, 0, 0}},
        {32, rm, C, {0, 0, C}},
        {33, 0xC0000040, 0, {0, C, C}},
        {40, 0x81800000, 0, {0, C, C}},
        {64, rm, C, {0, 0, C}},
        {96, rm, C, {0, 0, C}},
        {255, 0x00000103, 0, {0, 0, 0}},
    };
    for (auto& e : t) {
        Rig k;  // the default: the real AES's rule
        CHECK_EQ(one(k, dp_rs(MOV, true, 0, 0, 1, ROR, 2), rm, e.amount, C), e.result);
        CHECK_EQ(k.psr() & C, e.aes);
        for (int i = 0; i < 3; ++i) {
            O o;
            o.ror_reg_carry = alt[i];
            Rig m(o);
            CHECK_EQ(one(m, dp_rs(MOV, true, 0, 0, 1, ROR, 2), rm, e.amount, C), e.result);
            CHECK_EQ(m.psr() & C, e.alt[i]);
        }
    }
}

TEST(imm_rotate, "Immediate: 8 bits, rotate right by Shf*2 (DOC, SW p.16)") {
    Rig k;
    CHECK_EQ(one(k, dp_imm(MOV, false, 0, 0, 0xFF, 4), 0, 0, 0), 0xFF000000);
    Rig m;
    CHECK_EQ(one(m, dp_imm(MOV, false, 0, 0, 0xC3, 1), 0, 0, 0), 0xC0000030);
}

TEST(imm_carry, "Immediate rotate: C from bit 31 if rotated, else kept (INF, SW p.11)") {
    Rig k;
    one(k, dp_imm(MOV, true, 0, 0, 0xFF, 4), 0, 0, 0);
    CHECK_EQ(k.psr() & C, C);
    Rig m;
    one(m, dp_imm(MOV, true, 0, 0, 1), 0, 0, C);
    CHECK_EQ(m.psr() & C, C);
}

TEST(r15_rm_rn, "R15 as Rm is PC+8 with PSR; as Rn is PC+8 without (DOC, SW pp.1, 17)") {
    Rig k;
    k.load(A, {dp_reg(MOV, false, 0, 0, PC), dp_imm(ADD, false, 2, PC, 0)});
    k.start(N | V | A);
    k.step();
    CHECK_EQ(k.r(0), N | V | (A + 8));
    k.step();
    CHECK_EQ(k.r(2), A + 12);
}

TEST(r15_reg_shift, "R15 read +12 with a register shift (DS87 6.3.6, adapted; the real AES, test 10 C3, C4)") {
    Rig k;
    k.load(A, {dp_rs(MOV, false, 0, 0, PC, LSL, 2), dp_rs(ADD, false, 3, PC, 2, LSL, 2)});
    k.start(N | A);
    k.r(2) = 0;
    k.step();
    CHECK_EQ(k.r(0), N | (A + 12));
    k.step();
    CHECK_EQ(k.r(3), A + 4 + 12);
}

TEST(r15_as_rs, "R15 as Rs gives PC+8 without PSR (DS87 6.3.6, adapted)") {
    Rig k;
    CHECK_EQ(one(k, dp_rs(MOV, false, 0, 0, 1, LSL, PC), 1, 0, N), 1u << ((A + 8) & 0xFF));
}

TEST(r15_as_rs_privileged, "R15 as Rs in a privileged mode: the mode bits stay out of the amount (INF, 7a, DS87 6.3.6, adapted)") {
    for (uint32_t mode : {1u, 2u, 3u}) {
        Rig k;
        CHECK_EQ(one(k, dp_rs(MOV, false, 0, 0, 1, LSL, PC), 1, 0, mode), 1u << ((A + 8) & 0xFF));
    }
}

TEST(r15_write_pc_only, "Rd = R15, S clear: PC only, PSR kept (DOC, SW p.17; DS87 6.3.5)") {
    Rig k;
    k.load(A, {dp_reg(MOV, false, PC, 0, 1)});
    k.start(V | A);
    k.r(1) = 0xF0002003;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), V);
}

TEST(r15_write_user_s, "Rd = R15, S set, user: flags change, I F and mode do not (DOC, SW p.3)") {
    Rig k;
    k.load(A, {dp_reg(MOV, true, PC, 0, 1)});
    k.start(A);
    k.r(1) = 0xFC002003;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), N | Z | C | V);
}

TEST(r15_write_svc_s, "Rd = R15, S set, non-user: all 32 bits used (DOC, SW p.17)") {
    Rig k;
    k.load(A, {dp_reg(MOV, true, PC, 0, 1)});
    k.start(A | 3);
    k.cpu->reg_in_mode(3, 1) = 0xAC002002;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), N | C | I | F | 2);
}

TEST(pc_wraps, "PC arithmetic wraps within 26 bits (DOC, SW p.7)") {
    Rig k;
    k.load(0x3FFFFF8, {nop(), nop()});
    k.load(0, {nop()});
    k.start(0x3FFFFF8);
    k.step(3);
    CHECK_EQ(k.pc(), 8 + 4);
}

TEST(teqp_user, "TEQP in user mode: flags only (DOC, SW pp.3, 17)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 0xF3, 4)});  // #&F3000000
    k.start(A);
    k.step();
    CHECK_EQ(k.psr(), N | Z | C | V);
}

TEST(teqp_svc, "TEQP R15,#2 in SVC changes to IRQ mode, no branch (DOC, SW pp.17-18)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 2), nop()});
    k.start(A | 3);
    k.step();
    CHECK_EQ(k.psr(), 2);
    CHECK_EQ(k.pc(), A + 12);
}

TEST(cmp_not_rd, "CMP, CMN, TST, TEQ with S and Rd not R15 write no register (DOC, SW p.12)") {
    for (int op : {TST, TEQ, CMP, CMN}) {
        Rig k;
        CHECK_EQ(one(k, dp_reg(op, true, 0, 1, 2), 5, 5, 0), 0xDEADBEEF);
    }
}

TEST(enc_b, "A compare with S clear: no trap, Rd not written, flags kept (DOC, SW p.12; the real AES, test 5 part 2)") {
    for (int op : {TST, TEQ, CMP, CMN}) {
        Rig k;
        CHECK_EQ(one(k, dp_reg(op, false, 0, 1, 2), 5, 5, V), 0xDEADBEEF);
        CHECK_EQ(k.psr(), V);
        CHECK_EQ(k.pc(), A + 12);
    }
}

TEST(enc_b_pc, "A compare with S clear and Rd = R15: no write, no jump, flags kept (the real AES, test 5 part 2)") {
    Rig k;
    k.load(A, {dp_reg(CMP, false, PC, 1, 2), nop()});
    k.start(V | A);
    k.r(1) = 0x3000;
    k.r(2) = 0x1000;
    k.step();
    CHECK_EQ(k.pc(), A + 12);
    CHECK_EQ(k.psr(), V);
}

TEST(enc_a, "cpu.enc_a_bit7: bit 7 ignored (INF, SW85 p.10 'x'); trap alternative (SW p.14)") {
    const uint32_t word = dp_rs(MOV, false, 0, 0, 1, LSL, 2) | 0x80;
    Rig k;
    CHECK_EQ(one(k, word, 3, 4, 0), 0x30);
    O o;
    o.enc_a_bit7 = O::EncABit7::Traps;
    Rig m(o);
    CHECK_EQ(one(m, word, 3, 4, 0), 0xDEADBEEF);
    CHECK_EQ(m.pc(), 4 + 8);
}

TEST(mul_tripwire, "ARM2-only tripwire: a MUL word does not multiply on the ARM1 (DOC, DS13 13.3: multiply added); it is a family A register shift and follows cpu.enc_a_bit7 (INF)") {
    const uint32_t mul_r3_r1_r2 = 0xE0030291;
    Rig k;
    k.load(A, {mul_r3_r1_r2});
    k.start(A);
    k.r(0) = 0xDEADBEEF;
    k.r(1) = 3;
    k.r(2) = 4;
    k.r(3) = 0xFFFFFFFF;
    k.step();
    CHECK_EQ(k.r(3), 0xFFFFFFFF);
    CHECK_EQ(k.r(0), 0x30);
    O o;
    o.enc_a_bit7 = O::EncABit7::Traps;
    Rig m(o);
    m.load(A, {mul_r3_r1_r2});
    m.start(A);
    m.step();
    CHECK_EQ(m.pc(), 4 + 8);
}

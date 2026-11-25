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
// test_cycles.cpp, tests cycle counts and bus order
// version 2.0.0, 20 November 2026.

// Cycle counts and the manuals' example programs.

#include "ArmTest.hpp"

using O = ArmOptions;
static const uint32_t N = 1u << 31, C = 1u << 29;
static const uint32_t A = 0x1000, D = 0x8000;

static void check_cost(Rig& k, int s, int n) {
    Cost c = k.cost_of_next_instruction();
    CHECK_EQ(c.s, s);
    CHECK_EQ(c.n, n);
}

TEST(cycles_table, "Instruction cycles (DOC, SW p.32)") {
    Rig a;
    a.load(A, {dp_reg(ADD, false, 0, 1, 2)});
    a.start(A);
    check_cost(a, 1, 0);
    Rig b;
    b.load(A, {dp_rs(ADD, false, 0, 1, 2, LSL, 3)});
    b.start(A);
    check_cost(b, 1, 1);  // SW p.32: 2S; the real AES: the fetch after is N (time.reg_shift_next).
    Rig c;
    c.load(A, {dp_reg(MOV, false, PC, 0, 1)});
    c.start(A);
    c.r(1) = 0x2000;
    check_cost(c, 2, 1);
    Rig d;
    d.load(A, {dp_rs(MOV, false, PC, 0, 1, LSL, 3)});
    d.start(A);
    d.r(1) = 0x2000;
    check_cost(d, 3, 1);
    Rig e;
    e.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 0)});
    e.start(A);
    e.r(1) = D;
    check_cost(e, 2, 1);
    Rig f;
    f.load(A, {ldr_imm(LOAD | PRE | UP, PC, 1, 0)});
    f.start(A);
    f.r(1) = D;
    check_cost(f, 3, 2);
    Rig g;
    g.load(A, {ldr_imm(PRE | UP, 0, 1, 0)});
    g.start(A);
    g.r(1) = D;
    check_cost(g, 0, 2);
    Rig h;
    h.load(A, {ldr_reg(PRE | UP, 0, 1, 2) | (3u << 8) | (1u << 4)});
    h.start(A);
    h.r(1) = D;
    check_cost(h, 1, 2);
    Rig i;
    i.load(A, {branch(A, 0x2000)});
    i.start(A);
    check_cost(i, 2, 1);
    Rig j;
    j.load(A, {branch(A, 0x2000, true)});
    j.start(A);
    check_cost(j, 2, 1);
    Rig l;
    l.load(A, {swi()});
    l.start(A);
    check_cost(l, 2, 1);
}

TEST(cycles_failed, "Failed condition: one S, for every class (DOC, SW p.32)") {
    for (uint32_t w : {dp_reg(MOV, false, PC, 0, 1), ldr_imm(LOAD | PRE, 0, 1, 0),
                       block(UP, 1, 0xFF), branch(A, 0x2000), swi()}) {
        Rig k;
        k.load(A, {if_cond(0, w)});
        k.start(A);
        k.r(1) = D;
        check_cost(k, 1, 0);
    }
}

TEST(cycles_stm, "STM: (n-1)S + 2N (DOC, SW p.32)") {
    for (int n = 1; n <= 16; n++) {
        Rig k;
        k.load(A, {block(UP, 0, (1u << n) - 1)});
        k.start(A);
        k.r(0) = D;
        check_cost(k, n - 1, 2);
    }
}

TEST(cycles_ldm, "LDM: (n+1)S + 1N, the first word N and an internal cycle after the last (DOC, SW85 p.21). Decided by the real AES, tests 2 and 13 (results/real-aes-compare.md)") {
    for (int n = 1; n <= 14; n++) {
        Rig k;
        k.load(A, {block(UP | LOAD, 0, ((1u << n) - 1) << 1)});
        k.start(A);
        k.r(0) = D;
        check_cost(k, n + 1, 1);
    }
    Rig r;
    r.bus.poke(D, 0x2000);
    r.load(A, {block(UP | LOAD, 0, 1u << 15)});
    r.start(A);
    r.r(0) = D;
    check_cost(r, 1 + 1 + 1, 2);
}

TEST(cycles_reg_shift_next_fetch, "time.reg_shift_next: after a data-processing register shift the next fetch is N (INF, puzzles b explanation 1, from the real AES, test 2 RSH; results/real-aes-puzzles.md, b)") {
    Rig k;
    k.load(A, {dp_rs(ADD, false, 0, 1, 2, LSL, 3), dp_reg(ADD, false, 0, 1, 2)});
    k.start(A);
    k.cpu->step();
    CHECK_EQ(k.bus.log.size() == 2 && k.bus.log[1].kind == 'I', true);
    // The next step's first cycle is the fetch after the shift's internal cycle.
    k.bus.log.clear();
    k.cpu->step();
    CHECK_EQ(k.bus.log.size() > 0 && k.bus.log[0].kind == 'N', true);
}

TEST(cycles_reg_shift_two_internal, "time.reg_shift_next alternative: a second internal cycle, the next fetch sequential (UNK, puzzles b explanation 2)") {
    O o;
    o.time_reg_shift_next = O::RegShiftNext::TwoInternal;
    Rig k(o);
    k.load(A, {dp_rs(ADD, false, 0, 1, 2, LSL, 3), dp_reg(ADD, false, 0, 1, 2)});
    k.start(A);
    k.cpu->step();
    CHECK_EQ(k.bus.log.size() == 3 && k.bus.log[1].kind == 'I' && k.bus.log[2].kind == 'I', true);
    k.bus.log.clear();
    k.cpu->step();
    CHECK_EQ(k.bus.log.size() > 0 && k.bus.log[0].kind == 'S', true);
}

TEST(block_16, "All sixteen registers in one LDM with R15 last (DOC, SW85 p.15)") {
    Rig k;
    for (uint32_t i = 0; i < 15; i++) k.bus.poke(D + i * 4, 0x100 + i);
    k.bus.poke(D + 60, 0x2000);
    k.load(A, {block(UP | LOAD, 0, 0xFFFF)});
    k.start(A);
    k.r(0) = D;
    k.step();
    CHECK_EQ(k.r(0), 0x100);
    CHECK_EQ(k.r(14), 0x10E);
    CHECK_EQ(k.pc(), 0x2000 + 8);
}

// The manuals' examples are programs known to mean something, so a wrong core shows
// up as a wrong answer. Each ends on a branch to itself so the run has a clear stop.
static void run_to_loop(Rig& k, uint32_t stop) {
    for (int i = 0; i < 100000 && k.pc() != stop + 8; i++) k.step();
}

TEST(example_multiply, "Unsigned multiply loop (DOC, SW85 p.23, example 3)") {
    Rig k;
    const uint32_t end = A + 24;
    k.load(A, {dp_imm(MOV, false, 2, 0, 0),                    // MOV Rm,#0
               dp_reg(MOV, true, 0, 0, 0, LSR, 1),             // Loop MOVS Ra,Ra,LSR #1
               if_cond(2, dp_reg(ADD, false, 2, 2, 1)),        // ADDCS Rm,Rm,Rb
               dp_reg(ADD, false, 1, 1, 1),                    // ADD Rb,Rb,Rb
               if_cond(1, branch(A + 16, A + 4)),              // BNE Loop
               nop(),
               0xEAFFFFFE});
    k.start(A);
    k.r(0) = 1234;
    k.r(1) = 5678;
    run_to_loop(k, end);
    CHECK_EQ(k.r(2), 1234 * 5678);
}

TEST(example_divide, "Division and remainder (DOC, SW85 p.24, example 6)") {
    Rig k;
    const uint32_t div1 = A + 4, div2 = A + 28, end = A + 52;
    k.load(A, {dp_imm(MOV, false, 3, 0, 1),                      // MOV Rcnt,#1
               dp_imm(CMP, true, 0, 1, 0x80, 4),                 // Div1 CMP Rb,#&80000000
               if_cond(3, dp_reg(CMP, true, 0, 1, 0)),           // CMPCC Rb,Ra
               if_cond(3, dp_reg(MOV, false, 1, 0, 1, LSL, 1)),  // MOVCC Rb,Rb,ASL #1
               if_cond(3, dp_reg(MOV, false, 3, 0, 3, LSL, 1)),  // MOVCC Rcnt,Rcnt,ASL #1
               if_cond(3, branch(A + 20, div1)),                 // BCC Div1
               dp_imm(MOV, false, 2, 0, 0),                      // MOV Rc,#0
               dp_reg(CMP, true, 0, 0, 1),                       // Div2 CMP Ra,Rb
               if_cond(2, dp_reg(SUB, false, 0, 0, 1)),          // SUBCS Ra,Ra,Rb
               if_cond(2, dp_reg(ADD, false, 2, 2, 3)),          // ADDCS Rc,Rc,Rcnt
               dp_reg(MOV, true, 3, 0, 3, LSR, 1),               // MOVS Rcnt,Rcnt,LSR #1
               if_cond(1, dp_reg(MOV, false, 1, 0, 1, LSR, 1)),  // MOVNE Rb,Rb,LSR #1
               if_cond(1, branch(A + 48, div2)),                 // BNE Div2
               0xEAFFFFFE});
    k.start(A);
    k.r(0) = 1000003;
    k.r(1) = 37;
    run_to_loop(k, end);
    CHECK_EQ(k.r(2), 1000003 / 37);
    CHECK_EQ(k.r(0), 1000003 % 37);
}

TEST(example_unaligned, "Loading a word from an unknown alignment (DOC, SW85 p.25)") {
    Rig k;
    k.bus.poke(D, 0x44332211);
    k.bus.poke(D + 4, 0x88776655);
    k.load(A, {dp_imm(BIC, false, 1, 0, 3),                      // BIC Rb,Ra,#3
               block(UP | LOAD, 1, (1u << 3) | (1u << 4)),       // LDMIA Rb,{Rd,Rc}
               dp_imm(AND, false, 1, 0, 3),                      // AND Rb,Ra,#3
               dp_reg(MOV, true, 1, 0, 1, LSL, 3),               // MOVS Rb,Rb,LSL #3
               if_cond(1, dp_rs(MOV, false, 3, 0, 3, LSR, 1)),   // MOVNE Rd,Rd,LSR Rb
               if_cond(1, dp_imm(RSB, false, 1, 1, 32)),         // RSBNE Rb,Rb,#32
               if_cond(1, dp_rs(ORR, false, 3, 3, 4, LSL, 1)),   // ORRNE Rd,Rd,Rc,LSL Rb
               0xEAFFFFFE});
    k.start(A);
    k.r(0) = D + 2;
    run_to_loop(k, A + 28);
    CHECK_EQ(k.r(3), 0x66554433);
}

TEST(example_flags_return, "BICS PC,R14,#CFLAG clears C on return (DOC, SW85 p.26)") {
    Rig k;
    k.load(A, {dp_imm(BIC, true, PC, 14, 0x20, 4)});  // #&20000000
    k.start(A);
    k.r(14) = C | N | 0x2000;
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    CHECK_EQ(k.psr(), N);
}


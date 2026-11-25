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
// test_control.cpp, tests branches, banks, mode changes and reset
// version 2.0.0, 20 November 2026.

// Branches, register banks, mode changes, reset and the pipeline.

#include "ArmTest.hpp"

using O = ArmOptions;
static const uint32_t V = 1u << 28;
static const uint32_t I = 1u << 27, F = 1u << 26;
static const uint32_t A = 0x1000, D = 0x8000;

// abort strict (USER, 4 Nov 2026) needs to know when the ARM is in user mode.
TEST(core_bus_user_mode, "abort strict: the core tells the bus when its mode bits read user, from the next access on, the fetch included; an SWI and a reset clear it; LDRT in SVC does not set it (USER, 4 Nov 2026)") {
    Rig k;
    k.load(0x08, {branch(0x08, 0x08)});
    k.load(A, {ldr_imm(LOAD | UP | WB, 1, 2, 0), dp_imm(TEQ, true, PC, PC, 0), nop(), swi()});
    k.start(I | F | A | 3);
    k.r(2) = D;
    CHECK_EQ(k.bus.user_mode, false);
    k.step();
    CHECK_EQ(k.bus.log.size() >= 2, true);
    for (const Cyc& c : k.bus.log) CHECK_EQ(c.user, false);
    k.bus.log.clear();
    k.step();
    for (const Cyc& c : k.bus.log) CHECK_EQ(c.user, false);
    CHECK_EQ(k.bus.user_mode, true);
    k.bus.log.clear();
    k.step();
    CHECK_EQ(k.bus.log.size(), 1);
    CHECK_EQ(k.bus.log[0].user, true);
    k.bus.log.clear();
    k.step();
    CHECK_EQ(k.bus.log[0].user, true);
    CHECK_EQ(k.bus.log.back().user, false);
    CHECK_EQ(k.bus.user_mode, false);
    k.start(A);
    CHECK_EQ(k.bus.user_mode, true);
    k.cpu->set_reset(true);
    k.cpu->set_reset(false);
    k.bus.log.clear();
    k.step();
    for (const Cyc& c : k.bus.log) CHECK_EQ(c.user, false);
    CHECK_EQ(k.bus.user_mode, false);
}

TEST(branch_basic, "Branch: offset from PC+8, condition honoured (DOC, SW pp.7-8)") {
    Rig k;
    k.load(A, {branch(A, 0x2000)});
    k.start(A);
    k.step();
    CHECK_EQ(k.pc(), 0x2000 + 8);
    Rig m;
    m.load(A, {if_cond(0, branch(A, 0x2000)), nop()});
    m.start(A);
    m.step();
    CHECK_EQ(m.pc(), A + 12);
}

TEST(branch_back, "Backward branch; EAFFFFFE branches to itself (DOC, SW85 p.9)") {
    Rig k;
    k.load(A, {0xEAFFFFFE});
    k.start(A);
    k.step(3);
    CHECK_EQ(k.pc(), A + 8);
}

TEST(branch_link, "BL: R14 is next instruction plus PSR (DOC, SW p.7)") {
    Rig k;
    k.load(A, {branch(A, 0x2000, true)});
    k.start(V | A);
    k.step();
    CHECK_EQ(k.r(14), V | (A + 4));
    CHECK_EQ(k.pc(), 0x2000 + 8);
}

TEST(register_banks, "25 registers: R10-14 FIQ, R13-14 IRQ and SVC (DOC, SW p.5)") {
    Rig k;
    for (int mode = 0; mode < 4; mode++)
        for (int n = 0; n < 15; n++) k.cpu->reg_in_mode(mode, n) = 0;
    k.cpu->reg_in_mode(0, 9) = 0x90;
    k.cpu->reg_in_mode(0, 10) = 0xA0;
    k.cpu->reg_in_mode(0, 13) = 0xD0;
    k.cpu->reg_in_mode(1, 10) = 0xA1;
    k.cpu->reg_in_mode(1, 13) = 0xD1;
    k.cpu->reg_in_mode(2, 13) = 0xD2;
    k.cpu->reg_in_mode(3, 13) = 0xD3;
    CHECK_EQ(k.cpu->reg_in_mode(1, 9), 0x90);
    CHECK_EQ(k.cpu->reg_in_mode(2, 10), 0xA0);
    CHECK_EQ(k.cpu->reg_in_mode(3, 10), 0xA0);
    CHECK_EQ(k.cpu->reg_in_mode(0, 10), 0xA0);
    CHECK_EQ(k.cpu->reg_in_mode(1, 10), 0xA1);
    CHECK_EQ(k.cpu->reg_in_mode(0, 13), 0xD0);
    CHECK_EQ(k.cpu->reg_in_mode(1, 13), 0xD1);
    CHECK_EQ(k.cpu->reg_in_mode(2, 13), 0xD2);
    CHECK_EQ(k.cpu->reg_in_mode(3, 13), 0xD3);
}

TEST(fiq_bank_by_instructions, "FIQ banks R10-R14 only, by instructions: R8 and R9 are shared (DOC, SW p.5); the ARM2 banks R8-R14 (DOC, DS13 13.3)") {
    Rig k;
    k.load(A, {dp_imm(MOV, false, 8, 0, 0x81), dp_imm(MOV, false, 9, 0, 0x91), dp_imm(MOV, false, 10, 0, 0xA1),
               dp_imm(MOV, false, 11, 0, 0xB1), dp_imm(MOV, false, 12, 0, 0xC1), dp_imm(MOV, false, 13, 0, 0xD1),
               dp_imm(MOV, false, 14, 0, 0xE1), dp_imm(TEQ, true, PC, PC, 1), nop(),
               dp_imm(MOV, false, 8, 0, 0x82), dp_imm(MOV, false, 9, 0, 0x92), dp_imm(MOV, false, 10, 0, 0xA2),
               dp_imm(MOV, false, 11, 0, 0xB2), dp_imm(MOV, false, 12, 0, 0xC2), dp_imm(MOV, false, 13, 0, 0xD2),
               dp_imm(MOV, false, 14, 0, 0xE2), dp_imm(TEQ, true, PC, PC, 3), nop(), block(UP, 0, 0x7F00)});
    k.start(I | F | A | 3);
    k.cpu->reg_in_mode(3, 0) = D;
    k.step(19);
    const uint32_t seen_in_svc[7] = {0x82, 0x92, 0xA1, 0xB1, 0xC1, 0xD1, 0xE1};
    for (int i = 0; i < 7; i++) CHECK_EQ(k.bus.peek(D + 4 * i), seen_in_svc[i]);
    for (int n = 10; n <= 14; n++) CHECK_EQ(k.cpu->reg_in_mode(1, n), 0x02 + 0x10 * n);
    CHECK_EQ(k.psr() & 3, 3);
}

TEST(mode_delay_read, "cpu.mode_delay: next instruction reads the old bank (INF, SW85 p.5); alts") {
    struct {
        O::ModeDelay delay;
        uint32_t first, second;
    } t[] = {{O::ModeDelay::ReadsOldWritesNew, 0x333, 0x222},
             {O::ModeDelay::OldBankWholeInstruction, 0x333, 0x222},
             {O::ModeDelay::None, 0x222, 0x222}};
    for (auto& e : t) {
        O o;
        o.mode_delay = e.delay;
        Rig k(o);
        k.load(A, {dp_imm(TEQ, true, PC, PC, 2), dp_reg(MOV, false, 0, 0, 13),
                   dp_reg(MOV, false, 1, 0, 13)});
        k.start(A | 3);
        k.cpu->reg_in_mode(3, 13) = 0x333;
        k.cpu->reg_in_mode(2, 13) = 0x222;
        k.step(3);
        CHECK_EQ(k.r(0), e.first);
        CHECK_EQ(k.r(1), e.second);
    }
}

TEST(mode_delay_first_cycle_only, "cpu.mode_delay: a read after the first cycle sees the new bank (INF, DS87 6.3.5, 9.2)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 2), dp_rs(MOV, false, 0, 0, 13, LSL, 1)});
    k.start(A | 3);
    k.r(1) = 0;
    k.cpu->reg_in_mode(3, 13) = 0x333;
    k.cpu->reg_in_mode(2, 13) = 0x222;
    k.step(2);
    CHECK_EQ(k.r(0), 0x222);
}

TEST(exec_boot_irq_stack, "Write after TEQP reaches the new bank (SRC, Super1:156-157, ROM &185C); old-bank alt") {
    for (bool old_bank : {false, true}) {
        O o;
        if (old_bank) o.mode_delay = O::ModeDelay::OldBankWholeInstruction;
        Rig k(o);
        k.load(A, {dp_imm(TEQ, true, PC, PC, 2), dp_imm(MOV, false, 13, 0, 0x0F, 12)});
        k.start(A | 3);
        k.cpu->reg_in_mode(3, 13) = 0xE00;
        k.step(2);
        CHECK_EQ(k.cpu->reg_in_mode(2, 13), old_bank ? 0 : 0xF00);
        CHECK_EQ(k.cpu->reg_in_mode(3, 13), old_bank ? 0xF00 : 0xE00);
    }
}

TEST(exec_fiq_tuber, "Write after TEQP into FIQ R12 (SRC, Kernel:983-984, 996-997, ROM &BDC, &C10)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 0x42, 3), dp_imm(MOV, false, 12, 0, 0x01, 4)});
    k.start(I | A | 2);
    k.cpu->reg_in_mode(2, 12) = 0x777;
    k.step(2);
    CHECK_EQ(k.cpu->reg_in_mode(1, 12), 0x1000000);
    CHECK_EQ(k.cpu->reg_in_mode(2, 12), 0x777);
}

TEST(exec_fiq_table, "ADR after TEQP, used by the next instruction (SRC, Kernel:986-988, ROM &BE8)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 0x82, 3), dp_imm(ADD, false, 10, PC, 0x40),
               ldr_reg(LOAD | PRE | UP, 10, 10, 11, LSL, 2)});
    k.bus.poke(A + 4 + 8 + 0x40, 0xABCD);
    k.start(I | A | 1);
    k.cpu->reg_in_mode(1, 10) = 0x777;
    k.cpu->reg_in_mode(2, 11) = 0;
    k.step(3);
    CHECK_EQ(k.cpu->reg_in_mode(2, 10), 0xABCD);
    CHECK_EQ(k.cpu->reg_in_mode(1, 10), 0x777);
}

TEST(exec_bl_after_teqp, "BL after TEQP links in the new bank (SRC, Kernel:999-1000, ROM &C1C)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 0x82, 3), branch(A + 4, 0x2000, true),
               dp_imm(MOV, false, 5, 0, 0x55)});
    k.load(0x2000, {dp_reg(MOV, false, PC, 0, 14)});
    k.start(I | A | 1);
    k.cpu->reg_in_mode(1, 14) = 0x777;
    k.step(4);
    CHECK_EQ(k.r(5), 0x55);
    CHECK_EQ(k.cpu->reg_in_mode(1, 14), 0x777);
}

TEST(exec_load_user_regs, "LDR after TEQP loads the new bank (SRC, Middle:545-547, ROM &1514)") {
    Rig k;
    k.bus.poke(D, 0x1313);
    k.bus.poke(D + 4, 0x1414);
    k.load(A, {dp_imm(TEQ, true, PC, PC, 0x03, 3), ldr_imm(LOAD | PRE | UP, 13, 12, 0),
               ldr_imm(LOAD | PRE | UP, 14, 12, 4)});
    k.start(A | 3);
    k.cpu->reg_in_mode(0, 12) = D;
    k.cpu->reg_in_mode(3, 13) = 0x777;
    k.step(3);
    CHECK_EQ(k.cpu->reg_in_mode(0, 13), 0x1313);
    CHECK_EQ(k.cpu->reg_in_mode(0, 14), 0x1414);
    CHECK_EQ(k.cpu->reg_in_mode(3, 13), 0x777);
}

TEST(exec_guard_no_op, "No-op after TEQP, then a banked read sees the new bank (SRC, Middle:111-113, Kernel:922-924)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 1), 0xFAFFFFFE, dp_reg(MOV, false, 7, 0, 11)});
    k.start(A | 3);
    k.cpu->reg_in_mode(3, 11) = 0x333;
    k.cpu->reg_in_mode(1, 11) = 0x111;
    k.step(3);
    CHECK_EQ(k.r(7), 0x111);
}

TEST(mode_flush, "A mode change with a jump: the target sees the new bank (INF, SW p.17)") {
    Rig k;
    k.load(A, {dp_reg(MOV, true, PC, 0, 14)});
    k.load(0x2000, {dp_reg(MOV, false, 0, 0, 13)});
    k.start(A | 3);
    k.cpu->reg_in_mode(3, 14) = 0x2002;  // to IRQ mode
    k.cpu->reg_in_mode(3, 13) = 0x333;
    k.cpu->reg_in_mode(2, 13) = 0x222;
    k.step(2);
    CHECK_EQ(k.r(0), 0x222);
}

TEST(reset_sequence, "Reset: dummy fetches climb, then vector 0, SVC, I, F (DOC, SW p.27; HW)") {
    Rig k;
    k.load(A, {nop(), nop(), nop()});
    k.start(V | A);
    k.step();
    k.cpu->set_reset(true);
    k.step(3);
    CHECK_EQ(k.pc(), A + 12 + 12);
    CHECK_EQ(k.bus.log.back().addr, A + 12 + 8);
    k.cpu->set_reset(false);
    k.step();
    CHECK_EQ(k.pc(), 0 + 8);
    CHECK_EQ(k.psr(), V | I | F | 3);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), V | (A + 12 + 12 - 4));
}

TEST(reset_r14_alt, "cpu.reset_r14: R15 - 4 (INF, SW p.28); unadjusted alternative") {
    O o;
    o.reset_r14 = O::ResetR14::R15;
    Rig k(o);
    k.load(A, {nop()});
    k.start(A);
    k.cpu->set_reset(true);
    k.step();
    k.cpu->set_reset(false);
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 8 + 4);
}

TEST(power_on, "Power-on: the first step runs the reset sequence (DOC, SW p.27)") {
    Rig k;
    k.step();
    CHECK_EQ(k.pc(), 8);
    CHECK_EQ(k.psr(), I | F | 3);
}

TEST(pipeline_live, "Prefetch: a store over the next two words has no effect yet (DOC, SW p.1)") {
    Rig k;
    k.load(A, {ldr_imm(PRE | UP, 0, 1, 0), dp_imm(MOV, false, 2, 0, 1), dp_imm(MOV, false, 3, 0, 1),
               dp_imm(MOV, false, 4, 0, 1)});
    k.start(A);
    k.r(0) = dp_imm(MOV, false, 2, 0, 9);
    k.r(1) = A + 4;
    k.step(2);
    CHECK_EQ(k.r(2), 1);
    Rig m;
    m.load(A, {ldr_imm(PRE | UP, 0, 1, 0), nop(), nop(), dp_imm(MOV, false, 4, 0, 1)});
    m.start(A);
    m.r(0) = dp_imm(MOV, false, 4, 0, 9);
    m.r(1) = A + 12;
    m.step(4);
    CHECK_EQ(m.r(4), 9);
}

TEST(no_decoded_cache, "Vector code rewritten at run time takes effect (SRC, Kernel.txt:987-993)") {
    Rig k;
    k.load(0x1C, {dp_imm(MOV, false, 0, 0, 1)});
    k.load(A, {nop()});
    k.start(A);
    k.bus.poke(0x1C, dp_imm(MOV, false, 0, 0, 2));
    k.cpu->set_fiq(true);
    k.step(2);
    CHECK_EQ(k.r(0), 2);
}

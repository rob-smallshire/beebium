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
// test_exceptions.cpp, tests exceptions and interrupts
// version 2.0.0, 20 November 2026.

// Exceptions: SWI, undefined, IRQ, FIQ, address exceptions and aborts.

#include "ArmTest.hpp"

using O = ArmOptions;
static const uint32_t C_FLAG = 1u << 29, V = 1u << 28;
static const uint32_t I = 1u << 27, F = 1u << 26;
static const uint32_t A = 0x1000, D = 0x8000;
TEST(interrupt_will_be_taken, "The core says whether its next step takes an interrupt: a line set and its mask bit clear, FIQ or IRQ; the Tube wait asks it (DOC, SW pp.29-30; what-if 29)") {
    Rig k;
    k.cpu->set_r15(0x0C000003 | 0x1000);  // I and F set
    k.cpu->set_fiq(true);
    k.cpu->set_irq(true);
    CHECK_EQ(k.cpu->interrupt_will_be_taken(), false);
    k.cpu->set_r15(0x08000003 | 0x1000);  // F clear, I still set
    CHECK_EQ(k.cpu->interrupt_will_be_taken(), true);
    k.cpu->set_fiq(false);
    CHECK_EQ(k.cpu->interrupt_will_be_taken(), false);
    k.cpu->set_r15(0x00000003 | 0x1000);  // I clear too
    CHECK_EQ(k.cpu->interrupt_will_be_taken(), true);
    k.cpu->set_irq(false);
    CHECK_EQ(k.cpu->interrupt_will_be_taken(), false);
}

TEST(swi_entry, "SWI: R14_SVC is next instruction, vector 8, I set, F kept (DOC, SW pp.25, 30)") {
    Rig k;
    k.load(A, {swi(0x123456)});
    k.start(V | A);
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), V | (A + 4));
    CHECK_EQ(k.pc(), 8 + 8);
    CHECK_EQ(k.psr(), V | I | 3);
}

TEST(swi_failed, "SWI with failed condition does nothing (DOC, SW p.25)") {
    Rig k;
    k.load(A, {if_cond(0, swi()), nop()});
    k.start(A);
    k.step();
    CHECK_EQ(k.psr(), 0);
    CHECK_EQ(k.pc(), A + 12);
}

TEST(undef_entry, "Undefined: 110X and 1110 trap, R14 is instruction + 4 (DOC, SW pp.30, 36)") {
    for (uint32_t top : {0xCu, 0xDu, 0xEu}) {
        Rig k;
        k.load(A, {AL | (top << 24)});
        k.start(A);
        k.step();
        CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 4);
        CHECK_EQ(k.pc(), 4 + 8);
        CHECK_EQ(k.psr(), I | 3);
    }
}

TEST(q3_failed_undef, "Q3: traps on a failed condition (DOC, DS13; the real AES, test 8 C6)") {
    Rig k;
    k.load(A, {0x0E000000});
    k.start(A);
    k.step();
    CHECK_EQ(k.pc(), 4 + 8);
}

TEST(ldc_stc, "LDC and STC encodings use the undefined vector (DOC, SW85 p.19, SW p.36)") {
    for (uint32_t word : {0xED000000u, 0xEC000000u}) {
        Rig k;
        k.load(A, {word});
        k.start(A);
        k.step();
        CHECK_EQ(k.pc(), 4 + 8);
    }
}

TEST(coproc_tripwire, "ARM2-only tripwire: CDP, MCR, MRC, LDC and STC all take the undefined trap; the ARM1 has no coprocessor interface (DOC, DS13 13.3; SW p.36)") {
    for (uint32_t word : {0xEE000000u, 0xEE000010u, 0xEE100010u, 0xED900000u, 0xEC800000u}) {
        Rig k;
        k.load(A, {word});
        k.start(A);
        k.step();
        CHECK_EQ(k.pc(), 4 + 8);
        CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 4);
        CHECK_EQ(k.psr(), I | 3);
    }
}

TEST(irq_entry, "IRQ: R14_IRQ, vector &18, I set, return SUBS PC,R14,#4 (DOC, SW p.30)") {
    Rig k;
    k.load(A, {nop(), nop(), nop()});
    k.load(0x18, {dp_imm(SUB, true, PC, 14, 4)});
    k.start(V | A);
    k.step();
    k.cpu->set_irq(true);
    k.step();
    CHECK_EQ(k.psr(), V | I | 2);
    CHECK_EQ(k.pc(), 0x18 + 8);
    CHECK_EQ(k.cpu->reg_in_mode(2, 14), V | (A + 4 + 4));
    k.cpu->set_irq(false);
    k.step();
    CHECK_EQ(k.pc(), A + 4 + 8);
    CHECK_EQ(k.psr(), V);
}

TEST(fiq_entry, "FIQ: R14_FIQ, vector &1C, F and I set, beats IRQ (DOC, SW p.29)") {
    Rig k;
    k.load(A, {nop()});
    k.start(A);
    k.cpu->set_irq(true);
    k.cpu->set_fiq(true);
    k.step();
    CHECK_EQ(k.psr(), I | F | 1);
    CHECK_EQ(k.pc(), 0x1C + 8);
    CHECK_EQ(k.cpu->reg_in_mode(1, 14), A + 4);
}

TEST(masks, "I and F flags mask IRQ and FIQ (DOC, SW pp.29-30)") {
    Rig k;
    k.load(A, {nop()});
    k.start(I | F | A | 3);
    k.cpu->set_irq(true);
    k.cpu->set_fiq(true);
    k.step();
    CHECK_EQ(k.pc(), A + 12);
}

TEST(irq_is_level, "IRQ is a level: it fires again if still held when unmasked (DOC, HW)") {
    Rig k;
    k.load(A, {dp_imm(TEQ, true, PC, PC, 0), nop(), nop()});
    k.start(I | A | 3);
    k.cpu->set_irq(true);
    k.step();
    CHECK_EQ(k.psr(), 0);
    k.step();
    CHECK_EQ(k.psr(), I | 2);
    CHECK_EQ(k.cpu->reg_in_mode(2, 14), A + 4 + 4);
}

TEST(adx_r14_ldr, "LDR outside 64 MB traps, R14 instruction + 8 (DOC, SW 3.2 return; the real AES, test 7 C4 to C6)") {
    for (int offset : {8}) {
        Rig k;
        k.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 0)});
        k.start(V | A);
        k.r(0) = 0x5E471E1;
        k.r(1) = 0x04000000;
        k.step();
        CHECK_EQ(k.pc(), 0x14 + 8);
        CHECK_EQ(k.psr(), V | I | 3);
        CHECK_EQ(k.cpu->reg_in_mode(3, 14), V | (A + offset));
        CHECK_EQ(k.cpu->reg_in_mode(0, 0), 0x5E471E1);
    }
}

TEST(adx_r14_sntest_dump, "adx.r14: replay of the dev-board dump, R14 &200011C8 (USER, one case, !.Result1)") {
    Rig k;
    k.load(0x11C0, {ldr_imm(PRE | UP, 0, 10, 0)});
    k.start(C_FLAG | 0x11C0);
    k.r(0) = 1;
    k.r(10) = 0xFFFFFFFC;
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), 0x200011C8);
    CHECK_EQ(k.bus.peek(0x3FFFFFC), 0);
}

TEST(adx_return, "Return by SUBS PC,R14,#4 reaches the next instruction (DOC, SW 3.2)") {
    Rig k;
    k.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 0), dp_imm(MOV, false, 2, 0, 9)});
    k.load(0x14, {dp_imm(SUB, true, PC, 14, 4)});
    k.start(A);
    k.r(1) = 0x04000000;
    k.step(3);
    CHECK_EQ(k.r(2), 9);
}

TEST(adx_address_used, "The address used is checked, not the base (DS87 6.5.5, adapted; the real AES, test 7 C1, C3, C5, C6)") {
    Rig k;
    k.bus.poke(0x3FFFFFC, 0x4321);
    k.load(A, {ldr_imm(LOAD | PRE, 0, 1, 8)});
    k.start(A);
    k.r(1) = 0x04000004;
    k.step();
    CHECK_EQ(k.r(0), 0x4321);
    CHECK_EQ(k.pc(), A + 12);
    Rig p;
    p.load(A, {ldr_imm(LOAD | UP, 0, 1, 0x100)});
    p.start(A);
    p.r(1) = 0x03FFFFFC;
    p.step();
    CHECK_EQ(p.pc(), A + 12);
    CHECK_EQ(p.r(1), 0x040000FC);
}

TEST(adx_writeback, "The base is not written back (DOC, SW 3.2(1); the real AES, test 7 C6)") {
    for (bool written : {false}) {
        Rig k;
        k.load(A, {ldr_imm(LOAD | PRE | UP | WB, 0, 1, 4)});
        k.start(A);
        k.r(1) = 0x04000000;
        k.step();
        CHECK_EQ(k.cpu->reg_in_mode(0, 1), written ? 0x04000004 : 0x04000000);
    }
}

TEST(adx_store, "adx.store_to_load: faulting store writes nothing (DS87 5.3.3); wraps alt") {
    for (bool to_load : {true, false}) {
        O o;
        o.adx_store_to_load = to_load;
        Rig k(o);
        k.load(A, {ldr_imm(PRE | UP, 0, 1, 0)});
        k.start(A);
        k.r(0) = 0xABCD;
        k.r(1) = 0x04000100;
        k.step();
        CHECK_EQ(k.bus.peek(0x100), to_load ? 0 : 0xABCD);
        CHECK_EQ(k.pc(), 0x14 + 8);
    }
}

TEST(adx_ldm_first, "LDM over the top wraps, no trap: only the first address is checked (DOC, SW 3.2; the real AES, test 7 C2)") {
    Rig k;
    k.bus.poke(0x3FFFFF8, 1);
    k.bus.poke(0x3FFFFFC, 2);
    k.bus.poke(0, 3);
    k.bus.poke(4, 4);
    k.load(A, {block(UP | LOAD, 5, 0xF)});
    k.start(A);
    k.r(5) = 0x03FFFFF8;
    k.step();
    CHECK_EQ(k.pc(), A + 12);
    CHECK_EQ(k.r(0), 1);
    CHECK_EQ(k.r(3), 4);
}

TEST(adx_ldm_first_bad, "ldm.fault_loads: bad first address, LDM completes, base updated (DOC, SW 3.2(1), SW85 p.5)") {
    Rig k;
    k.bus.poke(0, 0xAAAA);
    k.bus.poke(4, 0xBBBB);
    k.load(A, {block(UP | LOAD | WB, 5, 0x3)});
    k.start(A);
    k.r(0) = 0x1111;
    k.r(5) = 0x04000000;
    k.step();
    CHECK_EQ(k.pc(), 0x14 + 8);
    CHECK_EQ(k.cpu->reg_in_mode(0, 0), 0xAAAA);
    CHECK_EQ(k.cpu->reg_in_mode(0, 1), 0xBBBB);
    CHECK_EQ(k.cpu->reg_in_mode(0, 5), 0x04000008);
}

TEST(adx_ldm_first_bad_alt, "ldm.fault_loads alternative: no register loaded, base updated (DS87 6.6.7, 6.6.8, adapted)") {
    O o;
    o.ldm_fault_loads = O::LdmFaultLoads::StopOverwriting;
    Rig k(o);
    k.load(A, {block(UP | LOAD | WB, 5, 0x3)});
    k.start(A);
    k.r(0) = 0x1111;
    k.r(5) = 0x04000000;
    k.step();
    CHECK_EQ(k.pc(), 0x14 + 8);
    CHECK_EQ(k.cpu->reg_in_mode(0, 0), 0x1111);
    CHECK_EQ(k.cpu->reg_in_mode(0, 5), 0x04000008);
}

TEST(adx_ldm_first_bad_pc, "ldm.fault_loads: bad first address never loads the PC (DOC, SW p.40; SW 3.2 return)") {
    Rig k;
    k.bus.poke(0, 0x2000);
    k.load(A, {block(UP | LOAD, 5, 1u << 15)});
    k.start(A);
    k.r(5) = 0x04000000;
    k.step();
    CHECK_EQ(k.pc(), 0x14 + 8);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 8);
}

static int fetches_at(const Rig& k, uint32_t addr) {
    int count = 0;
    for (const Cyc& y : k.bus.log) count += y.addr == addr;
    return count;
}

TEST(adx_ldm_pc_kept, "ldm.fault_loads: bad first address, no jump to the loaded PC (DOC, SW p.40); PSR kept in R14 (INF, same register)") {
    const uint32_t target = 0x2000;
    Rig k;
    k.bus.poke(0, V | target | 3);
    k.load(A, {block(UP | LOAD | PSRBIT, 5, 1u << 15)});
    k.start(A | 3);
    k.r(5) = 0x04000000;
    k.step();
    CHECK_EQ(fetches_at(k, target), 0);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 8 + 3);
}

TEST(adx_stm_no_write, "STM with a bad first address writes nothing (DS87 6.6.7, adapted)") {
    Rig k;
    k.load(A, {block(UP, 5, 0x3)});
    k.start(A);
    k.r(5) = 0x04000100;
    k.r(0) = 0x5A5A;
    k.r(1) = 0x5B5B;
    k.step();
    CHECK_EQ(k.bus.peek(0x100), 0);
    CHECK_EQ(k.pc(), 0x14 + 8);
}

TEST(adx_then_fiq, "Address exception, then straight to FIQ if enabled (DS87 5.3.9, adapted)") {
    Rig k;
    k.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 0)});
    k.start(A);
    k.r(1) = 0x04000000;
    k.cpu->set_fiq(true);
    k.step(1);  // FIQ is level-checked before the LDR runs, so here it comes first
    CHECK_EQ(k.pc(), 0x1C + 8);
    Rig m;
    m.load(A, {ldr_imm(LOAD | PRE | UP, 0, 1, 0)});
    m.start(A);
    m.r(1) = 0x04000000;
    m.step();
    m.cpu->set_fiq(true);
    m.step();
    CHECK_EQ(m.pc(), 0x1C + 8);
    CHECK_EQ(m.cpu->reg_in_mode(1, 14), I | 3 | (0x14 + 4));
}

TEST(abort_ldr, "Data abort on LDR: as if not executed, vector &10 (DOC, SW p.29)") {
    Rig k;
    k.bus.poke(D, 0x7777);
    k.bus.abort_at = D;
    k.load(A, {ldr_imm(LOAD | PRE | UP | WB, 0, 1, 0)});
    k.start(A);
    k.r(0) = 0x11;
    k.r(1) = D;
    k.step();
    CHECK_EQ(k.pc(), 0x10 + 8);
    CHECK_EQ(k.psr(), I | 3);
    CHECK_EQ(k.cpu->reg_in_mode(0, 0), 0x11);
    CHECK_EQ(k.cpu->reg_in_mode(0, 1), D);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 8);
}

static void setup_abort_ldm(Rig& k) {
    k.bus.poke(D, 1);
    k.bus.poke(D + 4, 2);
    k.bus.poke(D + 8, 3);
    k.bus.poke(D + 12, 0x2000);
    k.bus.abort_at = D + 8;
    k.load(A, {block(UP | LOAD | WB, 5, (1u << 0) | (1u << 1) | (1u << 5) | (1u << 15))});
    k.start(A);
    k.r(0) = 0xAA;
    k.r(1) = 0xBB;
    k.r(5) = D;
    k.step();
}

TEST(abort_ldm, "ldm.fault_loads: LDM completes, base kept updated, PC preserved (DOC, SW p.29, p.40)") {
    Rig k;
    setup_abort_ldm(k);
    CHECK_EQ(k.cpu->reg_in_mode(0, 5), D + 16);
    CHECK_EQ(k.pc(), 0x10 + 8);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 8);
    CHECK_EQ(k.cpu->reg_in_mode(0, 0), 1);
    CHECK_EQ(k.cpu->reg_in_mode(0, 1), 2);
}

TEST(abort_ldm_alt, "ldm.fault_loads alternative: loads stop, the one before is lost (DS87 6.6.8, adapted)") {
    O o;
    o.ldm_fault_loads = O::LdmFaultLoads::StopOverwriting;
    Rig k(o);
    setup_abort_ldm(k);
    CHECK_EQ(k.cpu->reg_in_mode(0, 5), D + 16);
    CHECK_EQ(k.pc(), 0x10 + 8);
    CHECK_EQ(k.cpu->reg_in_mode(0, 0), 1);
    CHECK_EQ(k.cpu->reg_in_mode(0, 1), 0xBB);
}

TEST(abort_ldm_no_wb, "ldm.fault_loads alternative: no writeback, the base is restored (DS87 6.6.8, adapted)") {
    O o;
    o.ldm_fault_loads = O::LdmFaultLoads::StopOverwriting;
    Rig k(o);
    k.bus.poke(D, 0xEEE);
    k.bus.abort_at = D + 8;
    k.load(A, {block(UP | LOAD, 5, (1u << 5) | (1u << 6) | (1u << 7))});
    k.start(A);
    k.r(5) = D;
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(0, 5), D);
}

TEST(abort_ldm_pc_kept, "ldm.fault_loads: abort, no jump to the loaded PC (DOC, SW p.40); PSR kept in R14 (INF, same register)") {
    const uint32_t target = 0x2000;
    Rig k;
    k.bus.abort_at = D;
    k.bus.poke(D + 4, V | target | 3);
    k.load(A, {block(UP | LOAD | PSRBIT, 5, (1u << 15) | 1)});
    k.start(A | 3);
    k.r(5) = D;
    k.step();
    CHECK_EQ(fetches_at(k, target), 0);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 8 + 3);
}

TEST(abort_stm, "Data abort in STM: completes, base updated (DOC, SW p.29)") {
    Rig k;
    k.bus.abort_at = D;
    k.load(A, {block(UP | WB, 5, 0x3)});
    k.start(A);
    k.r(5) = D;
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(0, 5), D + 8);
    CHECK_EQ(k.pc(), 0x10 + 8);
}

TEST(prefetch_abort, "Prefetch abort: at execution only, vector &0C, R14 + 4 (DOC, SW p.29)") {
    Rig k;
    k.load(A, {nop(), nop(), nop(), nop()});
    k.bus.abort_at = A + 12;
    k.start(A);
    k.step(3);
    CHECK_EQ(k.pc(), A + 12 + 8);
    k.step();
    CHECK_EQ(k.pc(), 0x0C + 8);
    CHECK_EQ(k.cpu->reg_in_mode(3, 14), A + 12 + 4);
}

TEST(prefetch_abort_skipped, "Prefetch abort has no effect if a branch discards it (INF, DS87 5.3.4, adapted)") {
    Rig k;
    k.load(A, {nop(), branch(A + 4, 0x2000), nop(), nop()});
    k.bus.abort_at = A + 12;
    k.start(A);
    k.step(3);
    CHECK_EQ(k.pc(), 0x2000 + 12);
}

TEST(adx_store_r15, "adx.store_to_load alt: a wrapped store of R15 stores PC + 12 and PSR (INF, DS87 6.5.4, adapted)") {
    O o;
    o.adx_store_to_load = false;
    Rig k(o);
    k.load(A, {ldr_imm(PRE | UP, PC, 1, 0)});
    k.start(V | A);
    k.r(0) = 0x12345678;
    k.r(1) = 0x04000100;
    k.step();
    CHECK_EQ(k.bus.peek(0x100), V | (A + 12));
}

TEST(adx_store_byte, "adx.store_to_load alt: a wrapped STRB changes one byte (DOC, SW p.19 byte store)") {
    O o;
    o.adx_store_to_load = false;
    Rig k(o);
    k.bus.poke(0x100, 0x11223344);
    k.load(A, {ldr_imm(PRE | UP | BYTE, 0, 1, 0)});
    k.start(A);
    k.r(0) = 0xAABBCCDD;
    k.r(1) = 0x04000101;
    k.step();
    CHECK_EQ(k.bus.peek(0x100), 0x1122DD44);
    CHECK_EQ(k.pc(), 0x14 + 8);
}

TEST(abort_user_bank_base, "Abort with user-bank writeback leaves the current R13 alone (DOC, SW85 p.16, stated for STM; INF for LDM)") {
    Rig k;
    k.bus.abort_at = D + 4;
    k.load(A, {block(UP | WB | LOAD | PSRBIT, 13, 0x3)});
    k.start(A | 3);
    k.cpu->reg_in_mode(3, 13) = D;
    k.cpu->reg_in_mode(0, 13) = 0x111;
    k.step();
    CHECK_EQ(k.cpu->reg_in_mode(3, 13), D);
    CHECK_EQ(k.cpu->reg_in_mode(0, 13), D + 8);
    Rig m;
    m.bus.abort_at = D;
    m.load(A, {block(UP | WB | LOAD | PSRBIT, 13, 1u << 15)});
    m.start(A | 3);
    m.cpu->reg_in_mode(3, 13) = D;
    m.cpu->reg_in_mode(0, 13) = 0x111;
    m.step();
    CHECK_EQ(m.cpu->reg_in_mode(3, 13), D);
    CHECK_EQ(m.cpu->reg_in_mode(0, 13), D + 4);
}

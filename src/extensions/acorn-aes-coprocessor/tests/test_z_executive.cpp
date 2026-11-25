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
// test_z_executive.cpp, tests through the real Executive, last
// version 2.0.0, 20 November 2026.

// The real Executive's exception paths on the headless board: no Beebium and no Tube chip.
// Its name sorts last, so these run last, and the address-exception report is the last of
// all (USER, 31 Oct 2026: it ends the program). Spec: notes/test-specs.md, part 3.
// Expected text is written out from the Executive source, never from a run.

#include "AesBoardTest.hpp"
#include "RomCheck.hpp"

#include <cstring>
#include <string>

using Abort = AesOptions::MachineAbort;

static const uint32_t PROG = 0x1000, HANDLER_A = 0x1100, HANDLER_B = 0x1200;
static const uint32_t EMPTY = TUBE + 0x800000;  // Outside abort strict's map (USER, 31 Oct 2026).
static const uint32_t OUTSIDE = 0x4000000;      // First address above 64 MB (DOC, SW 3.2).
static const uint32_t C_FLAG = 1u << 29, V_FLAG = 1u << 28;
static const uint32_t PC_MASK = 0x3FFFFFC;
static const uint32_t UNDEFINED = AL | (0xCu << 24);  // DOC, SW p.36.
static const uint32_t COUNT = dp_imm(ADD, false, 9, 9, 1);
static const uint32_t SETENV = 0x12, UNUSEDSWI = 0x19;  // DOC, UTIL 2.4.19, 2.4.26.
static const long kLimit = 2000000;
static const int kWaitReads = 64;

static const std::vector<uint8_t>& executive_rom() {
    static const std::vector<uint8_t> image = rom_from_env();
    return image;
}

// A text the Executive prints, from the ROM's facts file.
static const std::string& said(const char* key) {
    static const RomFacts facts = rom_facts_for(executive_rom());
    return facts.at(key);
}

// Says "space free, no data" and records what is sent, so that the Executive can print and
// then wait. Tube registers count from 0: R1DATA 1, R2STAT 2, R2DATA 3 (SRC, Kernel:101-108).
struct SilentHost : TubePort {
    std::string text;
    std::vector<int> r2;
    int waiting_reads = 0;
    uint8_t read(int reg) override {
        waiting_reads = reg == 2 ? waiting_reads + 1 : 0;
        return 0x40;
    }
    void write(int reg, uint8_t value) override {
        waiting_reads = 0;
        if (reg == 1) text += char(value);
        if (reg == 3) r2.push_back(value);
    }
    bool waiting() const { return waiting_reads >= kWaitReads; }
    void forget() {
        text.clear();
        r2.clear();
        waiting_reads = 0;
    }
};

// A fresh machine, booted until the Executive first waits for the host (SRC, Super1:248).
// Its handlers are in place by then (Super1:243-244). A program then starts in user mode,
// as *GO would leave it, without the Tube conversation in between.
struct Executive {
    AesOptions o;
    AesTiming timing;
    SilentHost host;
    AesMemory mem;
    ArmCore cpu;

    explicit Executive(Abort abort_setting = Abort::Off, int mb = 4)
        : o(options(abort_setting, mb)), timing(o), mem(o, timing, host), cpu(mem, o.cpu) {
        CHECK_EQ(executive_rom().size(), 0x4000);
        mem.set_rom(executive_rom());
        run_to_wait();
    }
    static AesOptions options(Abort abort_setting, int mb) {
        AesOptions options;
        options.abort = abort_setting;
        options.ram = mb;
        return options;
    }
    // As AesTube::restart does it: the reset line, then the board's own reset (SRC, our code).
    void reset() {
        cpu.set_reset(true);
        mem.reset();
        cpu.set_reset(false);
        host.forget();
    }
    void run_to_wait() {
        for (long i = 0; i < kLimit && !host.waiting(); i++) cpu.step();
        CHECK_EQ(host.waiting(), true);
    }
    void run_to(uint32_t addr) {
        for (long i = 0; i < kLimit && next_pc() != addr; i++) cpu.step();
        CHECK_EQ(next_pc(), addr);
    }
    uint32_t next_pc() const { return (cpu.r15() & PC_MASK) - 8; }
    void put(uint32_t addr, std::initializer_list<uint32_t> words) {
        for (uint32_t w : words) {
            mem.write_word(addr, w, false);
            addr += 4;
        }
    }
    uint32_t& r(int n) { return cpu.reg_in_mode(0, n); }
    void start(uint32_t r15 = PROG) {
        host.forget();
        cpu.set_r15(r15);
    }
};

static std::string hex8(uint32_t value) {
    char text[9];
    snprintf(text, sizeof text, "%08X", value);
    return text;
}

// R0 to R14 hold &A0 + n, so a slot out of place shows. SRC, Middle:596-644, which is the
// format of the dev board's own dump (real output, its !.Result1 file).
static void set_marked_registers(Executive& e) {
    for (int n = 0; n < 15; n++) e.r(n) = 0xA0 + n;
}

static std::string dump_of(uint32_t r2, uint32_t r15, const char* flags) {
    std::string text = said("text.dump");
    for (int n = 0; n < 16; n++) {
        text += n % 4 == 0 ? "\n\r" : " ";
        text += "R" + std::to_string(n) + (n < 10 ? "  = " : " = ");
        text += hex8(n == 15 ? r15 : n == 2 ? r2 : 0xA0 + n);
    }
    return text + "\n\r" + said("said.mode_usr") + flags + "\n\r";
}

// SRC, Middle:594-595, Super1:283-287: the way out is OSWORD 1, 0 out and 5 back (AN p.23).
static void check_program_ended(Executive& e, const std::string& report) {
    CHECK_EQ(e.host.text == report, true);
    if (e.host.text != report) printf("  got [%s]\n  not [%s]\n", e.host.text.c_str(), report.c_str());
    CHECK_EQ(e.host.r2 == std::vector<int>({0x08, 0x01, 0x00, 0x05}), true);
    CHECK_EQ(e.cpu.r15() & 3, 3);
}

TEST(exec_boots_to_wait, "Real ROM, tied to its SHA-1 (USER, 1 Nov 2026), 4 MB: banner as in the ROM file at &1938, then one zero byte, then a wait on R2 (SRC, ROM bytes; Super1:192, 215-248; DOC, AN p.28)") {
    NEED_ROM(executive_rom());
    Executive e;
    CHECK_EQ(e.host.text == "\n\r" + said("text.banner") + "4096K\n\r\n\r" + std::string(1, '\0'), true);
    CHECK_EQ(e.host.r2.size(), 0);
}

TEST(exec_swi_own_handler, "Real ROM: SWI &200 goes to the handler set by UnusedSWI, which returns through the old address, V cleared; the default handler just returns (DOC, UTIL 2.4.26, 2.4)") {
    NEED_ROM(executive_rom());
    Executive e;
    e.put(PROG, {swi(UNUSEDSWI), dp_reg(MOV, false, 8, 0, 0), swi(0x200), branch(PROG + 12, PROG + 12)});
    e.put(HANDLER_A, {COUNT, dp_reg(MOV, false, PC, 0, 8)});
    e.r(0) = HANDLER_A;
    e.r(9) = 0;
    e.start(V_FLAG | PROG);
    e.run_to(PROG + 12);
    CHECK_EQ(e.r(9), 1);
    CHECK_EQ(e.r(8) != 0 && e.r(8) < std::stoul(said("workspace_limit"), nullptr, 0), true);
    CHECK_EQ(e.cpu.r15() & (V_FLAG | 3), 0);

    Executive plain;
    plain.put(PROG, {swi(0x200), branch(PROG + 4, PROG + 4)});
    plain.start(V_FLAG | PROG);
    plain.run_to(PROG + 4);
    CHECK_EQ(plain.cpu.r15() & (V_FLAG | 3), 0);
    CHECK_EQ(plain.host.text.size(), 0);
}

TEST(exec_setenv_handlers, "Real ROM: SetEnv gives the previous limits and handlers; undefined and address exception reach the program's own, and go on from them (DOC, UTIL 2.4.19; SW 3.2, 3.6)") {
    NEED_ROM(executive_rom());
    Executive e;
    e.put(PROG, {swi(SETENV), UNDEFINED, ldr_imm(LOAD | UP | PRE, 8, 8, 0), branch(PROG + 12, PROG + 12)});
    e.put(HANDLER_A, {COUNT, dp_reg(MOV, true, PC, 0, 14)});
    e.put(HANDLER_B, {COUNT, dp_imm(SUB, true, PC, 14, 4)});
    for (int n = 0; n < 8; n++) e.r(n) = 0;
    e.r(3) = 2;
    e.r(4) = HANDLER_A;
    e.r(7) = HANDLER_B;
    e.r(8) = OUTSIDE;
    e.r(9) = 0;
    e.start();
    e.run_to(PROG + 12);
    CHECK_EQ(e.r(9), 2);
    CHECK_EQ(e.r(8), OUTSIDE);
    CHECK_EQ(e.r(1), 4 * MB - 20 * 1024);
    CHECK_EQ(e.r(2), 4 * MB);
    CHECK_EQ(e.r(3), 1);
    for (int n = 4; n < 8; n++) CHECK_EQ((e.r(n) >> 24), 3);
    CHECK_EQ(e.host.text.size(), 0);
}

TEST(exec_undef_report, "Real ROM: its own undefined handler prints the report and the dump, then ends the program (SRC, Middle:579-595; R14 instruction + 4, DOC, SW 3.6)") {
    NEED_ROM(executive_rom());
    Executive e;
    e.put(PROG, {UNDEFINED});
    set_marked_registers(e);
    e.start(PROG);
    e.run_to_wait();
    const uint32_t at = PROG + 4;
    check_program_ended(e, said("text.undefined") + hex8(at) + "\n\r" + dump_of(0xA2, at, "nzcvif"));
}

TEST(exec_abort_default_never, "Real ROM, abort off, the default: the load and the jump that abort below print nothing, and no abort happens (USER, Plugin option 2; DOC low-res, ABORT tied low)") {
    NEED_ROM(executive_rom());
    Executive e;
    e.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0), branch(PROG + 4, PROG + 4)});
    e.r(2) = EMPTY;
    e.start();
    e.run_to(PROG + 4);
    CHECK_EQ(e.host.text.size(), 0);
    CHECK_EQ(e.cpu.r15() & 3, 0);
}

TEST(exec_data_abort_report, "Real ROM, abort strict, program in user mode: its own data abort handler prints the report and the dump, then ends the program (SRC, Middle:654-661; R14 instruction + 8, DOC, SW 3.3)") {
    NEED_ROM(executive_rom());
    Executive e(Abort::Strict);
    e.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0)});
    set_marked_registers(e);
    e.r(2) = EMPTY;
    e.start(PROG);
    e.run_to_wait();
    const uint32_t at = PROG + 8;
    check_program_ended(e, said("text.data_abort") + hex8(at) + "\n\r" + dump_of(EMPTY, at, "nzcvif"));
}

TEST(exec_prefetch_abort_report, "Real ROM, abort strict, program in user mode: its own prefetch abort handler prints the report and the dump, then ends the program (SRC, Middle:646-653; R14 that address + 4, DOC, SW 3.3)") {
    NEED_ROM(executive_rom());
    Executive e(Abort::Strict);
    e.put(PROG, {dp_reg(MOV, false, PC, 0, 2)});
    set_marked_registers(e);
    e.r(2) = EMPTY;
    e.start(PROG);
    e.run_to_wait();
    const uint32_t at = EMPTY + 4;
    check_program_ended(e, said("text.prefetch_abort") + hex8(at) + "\n\r" + dump_of(EMPTY, at, "nzcvif"));
}

TEST(exec_strict_boots, "Real ROM, abort strict, each RAM size: the Executive boots to its banner, because its RAM probe runs before any user mode (USER, 4 Nov 2026; SRC, the probe at ROM &1878, in IRQ mode)") {
    NEED_ROM(executive_rom());
    for (int mb : {2, 4}) {
        Executive e(Abort::Strict, mb);
        CHECK_EQ(e.host.text == "\n\r" + said("text.banner") + std::to_string(mb * 1024) + "K\n\r\n\r" + std::string(1, '\0'), true);
        CHECK_EQ(e.host.r2.size(), 0);
    }
}

TEST(exec_strict_user_abort, "Real ROM, abort strict: a user program reading just above RAM gets the Executive's data abort report and the dump (USER, 4 Nov 2026; SRC, Middle:654-661; R14 instruction + 8, DOC, SW 3.3)") {
    NEED_ROM(executive_rom());
    Executive e(Abort::Strict);
    e.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0)});
    set_marked_registers(e);
    e.r(2) = 4 * MB;
    e.start(PROG);
    e.run_to_wait();
    const uint32_t at = PROG + 8;
    check_program_ended(e, said("text.data_abort") + hex8(at) + "\n\r" + dump_of(4 * MB, at, "nzcvif"));
}

TEST(exec_strict_lax_after_reset, "Real ROM, abort strict: after a user program has started checking, a reset puts it back to lax, and the Executive boots again to its banner (USER, 4 Nov 2026; SRC, ROM &1824-&1878, no branch around the probe)") {
    NEED_ROM(executive_rom());
    Executive e(Abort::Strict);
    e.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0)});
    e.r(2) = 4 * MB;
    e.start(PROG);
    e.run_to_wait();
    CHECK_EQ(e.mem.strict_on(), true);
    CHECK_EQ(e.host.text.find(said("text.data_abort")) == 0, true);
    e.reset();
    e.run_to_wait();
    CHECK_EQ(e.host.text.find(said("text.banner")) != std::string::npos, true);
    CHECK_EQ(e.host.text.find(said("text.data_abort")) == std::string::npos, true);
}

TEST(exec_adx_report, "Real ROM, run last: its own address-exception handler prints the report and the dump, then ends the program (SRC, Middle:662-669; R14 instruction + 8, DOC, SW 3.2)") {
    NEED_ROM(executive_rom());
    CHECK_EQ(strstr(test_list().back().name, "run last") != nullptr, true);
    Executive e;
    e.put(PROG, {ldr_imm(LOAD | UP | PRE, 1, 2, 0)});
    set_marked_registers(e);
    e.r(2) = OUTSIDE;
    e.start(C_FLAG | PROG);
    e.run_to_wait();
    const uint32_t at = C_FLAG | (PROG + 8);
    check_program_ended(e, said("text.address_exception") + hex8(at) + "\n\r" + dump_of(OUTSIDE, at, "nzCvif"));
}

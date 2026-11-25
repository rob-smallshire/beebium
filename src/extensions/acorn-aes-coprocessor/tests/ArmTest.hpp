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
// ArmTest.hpp, a test rig: the core on a simple memory
// version 2.0.0, 20 November 2026.

// Test rig for ArmCore. The memory is flat so that no board guess enters a CPU test,
// and it logs each cycle because the cycle tests compare against SW p.32.

#pragma once

#include "../ArmCore.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*run)();
};
std::vector<TestCase>& test_list();
extern int g_checks;
extern int g_failures;
extern std::string g_skip_why;

struct TestAdd {
    TestAdd(const char* name, void (*run)()) { test_list().push_back({name, run}); }
};
#define TEST(id, name)                       \
    static void id();                        \
    static TestAdd add_##id(name, id);       \
    static void id()

// USER: a test that cannot run says why, and the run counts it. With
// AES_REQUIRE_ALL set it is a failure, on the model of Beebium's BEEBIUM_REQUIRE_MDNS (SRC).
#define NEED_ROM(image)                        \
    do {                                       \
        g_skip_why = rom_problem(image);       \
        if (!g_skip_why.empty()) return;       \
    } while (0)

#define CHECK_EQ(actual, expected)                                                       \
    do {                                                                                 \
        g_checks++;                                                                      \
        unsigned long long a_ = (unsigned long long)(uint32_t)(actual);                  \
        unsigned long long e_ = (unsigned long long)(uint32_t)(expected);                \
        if (a_ != e_) {                                                                  \
            g_failures++;                                                                \
            printf("  FAIL %s:%d: %s = %llX, expected %llX\n", __FILE__, __LINE__,       \
                   #actual, a_, e_);                                                     \
        }                                                                                \
    } while (0)

struct Cyc {
    char kind;  // 'I' stands apart from 'S' for the log, but costs as S (DOC, SW p.32).
    uint32_t addr;
    bool user = false;
};

class TestBus : public ArmBus {
public:
    TestBus() { mem = (uint8_t*)calloc(0x4000000, 1); }
    ~TestBus() override { free(mem); }
    uint32_t peek(uint32_t addr) const {
        uint32_t v;
        memcpy(&v, mem + addr, 4);
        return v;
    }
    void poke(uint32_t addr, uint32_t v) { memcpy(mem + addr, &v, 4); }

    uint32_t read_word(uint32_t addr, bool seq) override {
        note(seq, addr);
        return peek(addr & 0x3FFFFFC);
    }
    uint32_t read_byte(uint32_t addr, bool seq) override {
        note(seq, addr);
        return mem[addr];
    }
    void write_word(uint32_t addr, uint32_t v, bool seq) override {
        poke(addr & 0x3FFFFFC, v);
        note(seq, addr);
    }
    void write_byte(uint32_t addr, uint8_t v, bool seq) override {
        mem[addr] = v;
        note(seq, addr);
    }
    void internal(uint32_t addr) override { log.push_back({'I', addr}); }

    std::vector<Cyc> log;
    uint32_t abort_at = 0xFFFFFFFF;
    uint8_t* mem;

private:
    void note(bool seq, uint32_t addr) {
        log.push_back({seq ? 'S' : 'N', addr, user_mode});
        if (addr == abort_at) abort_line = true;
    }
};

struct Cost {
    int s = 0;
    int n = 0;
};

// Each test makes its own Rig because the options are fixed at construction.
struct Rig {
    TestBus bus;
    ArmOptions opt;
    ArmCore* cpu = nullptr;

    explicit Rig(ArmOptions o = ArmOptions()) : opt(o) { cpu = new ArmCore(bus, opt); }
    ~Rig() { delete cpu; }

    void load(uint32_t addr, std::initializer_list<uint32_t> words) {
        for (uint32_t w : words) {
            bus.poke(addr, w);
            addr += 4;
        }
    }
    void start(uint32_t r15) {
        cpu->set_r15(r15);
        bus.log.clear();
    }
    uint32_t& r(int n) { return cpu->reg_in_mode(cpu->r15() & 3, n); }
    void step(int count = 1) {
        for (int i = 0; i < count; i++) cpu->step();
    }
    uint32_t psr() const { return cpu->r15() & 0xFC000003; }
    uint32_t pc() const { return cpu->r15() & 0x03FFFFFC; }

    // SW p.32 counts cycles incrementally: the type of an instruction's first
    // cycle was set by the instruction before it (DOC, DS87 ch.9, seq column,
    // adapted). So: every cycle of this step but the first, plus the first
    // cycle of the next step.
    Cost cost_of_next_instruction() {
        Cost c;
        bus.log.clear();
        cpu->step();
        for (size_t i = 1; i < bus.log.size(); i++) add(c, bus.log[i]);
        bus.log.clear();
        cpu->step();
        add(c, bus.log[0]);
        return c;
    }

private:
    static void add(Cost& c, const Cyc& y) {
        if (y.kind == 'N') {
            c.n++;
        } else {
            c.s++;
        }
    }
};

// Encoders, so that a test can be read against the bit diagrams in SW pp.7, 13, 16,
// 19, 23 and 25 instead of as raw hex.
const uint32_t AL = 0xE0000000;
enum Op { AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN };
enum Sh { LSL, LSR, ASR, ROR };
const int PC = 15;

inline uint32_t if_cond(uint32_t cond, uint32_t word) { return (word & 0x0FFFFFFF) | (cond << 28); }
inline uint32_t dp_imm(int op, bool s, int rd, int rn, uint32_t imm8, int rot = 0) {
    return AL | (1u << 25) | (uint32_t(op) << 21) | (uint32_t(s) << 20) | (rn << 16) | (rd << 12) |
           (rot << 8) | imm8;
}
inline uint32_t dp_reg(int op, bool s, int rd, int rn, int rm, int type = LSL, int amount = 0) {
    return AL | (uint32_t(op) << 21) | (uint32_t(s) << 20) | (rn << 16) | (rd << 12) |
           (amount << 7) | (type << 5) | rm;
}
inline uint32_t dp_rs(int op, bool s, int rd, int rn, int rm, int type, int rs) {
    return AL | (uint32_t(op) << 21) | (uint32_t(s) << 20) | (rn << 16) | (rd << 12) | (rs << 8) |
           (type << 5) | (1u << 4) | rm;
}
inline uint32_t nop() { return dp_reg(MOV, false, 0, 0, 0); }

const uint32_t LOAD = 1u << 20, WB = 1u << 21, BYTE = 1u << 22, UP = 1u << 23, PRE = 1u << 24;
inline uint32_t ldr_imm(uint32_t flags, int rd, int rn, uint32_t off) {
    return AL | (1u << 26) | flags | (rn << 16) | (rd << 12) | off;
}
inline uint32_t ldr_reg(uint32_t flags, int rd, int rn, int rm, int type = LSL, int amount = 0) {
    return AL | (3u << 25) | flags | (rn << 16) | (rd << 12) | (amount << 7) | (type << 5) | rm;
}
inline uint32_t ldr_rs(uint32_t flags, int rd, int rn, int rm, int type, int rs) {
    return AL | (3u << 25) | flags | (rn << 16) | (rd << 12) | (rs << 8) | (type << 5) | (1u << 4) |
           rm;
}
const uint32_t PSRBIT = 1u << 22;
inline uint32_t block(uint32_t flags, int rn, uint32_t list) {
    return AL | (1u << 27) | flags | (rn << 16) | list;
}
// The offset is taken from address + 8 because of prefetch (DOC, SW p.8).
inline uint32_t branch(uint32_t at, uint32_t target, bool link = false) {
    return AL | (5u << 25) | (uint32_t(link) << 24) | (((target - at - 8) >> 2) & 0xFFFFFF);
}
inline uint32_t swi(uint32_t comment = 0) { return AL | (0xFu << 24) | comment; }

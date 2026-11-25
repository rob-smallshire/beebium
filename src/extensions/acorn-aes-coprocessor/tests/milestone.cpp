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
// milestone.cpp, runs the Executive from reset to its first Tube access
// version 2.0.0, 20 November 2026.

// Runs the real Executive under each RAM size and abort setting. Prints what the
// emulator did: experiments, not evidence of what an AES does. Run with run-milestone.sh.

#include "../AesMemory.hpp"

#include <cstdio>
#include <string>
#include <vector>

using Abort = AesOptions::MachineAbort;
using Absent = AesOptions::RamAbsentBank;

// SRC, Kernel:63: the boot code stores the RAM size it found here (Super1, SIZERX).
static const uint32_t kStrAcc = 0xF00;
static const long kFirstAccessLimit = 20000000, kStrictLimit = 3000000;
static const uint32_t kPcMask = 0x3FFFFFC;

// Says the host is always ready and never has data, so that the Executive can print and
// then wait. SRC, Kernel:234-237: status bit &40 is space available. R1DATA is register 1.
struct QuietHost : TubePort {
    bool seen = false, was_write = false;
    int reg = -1;
    std::string text;
    long accesses = 0;
    void note(int r, bool write) {
        accesses++;
        if (!seen) {
            reg = r;
            was_write = write;
        }
        seen = true;
    }
    uint8_t read(int r) override {
        note(r, false);
        return 0x40;
    }
    void write(int r, uint8_t value) override {
        note(r, true);
        if (r == 1) text += char(value);
    }
};

struct AbortRecord {
    long step;
    uint32_t address, value;
    bool write;
    uint32_t pc;
};

// Sees each abort as the board raises it; the core clears the line afterwards.
class WatchedMemory : public AesMemory {
public:
    using AesMemory::AesMemory;
    std::vector<AbortRecord> aborts;
    long step = 0;
    uint32_t pc = 0;
    long s_cycles = 0, n_cycles = 0, i_cycles = 0;
    uint32_t first_tube_address = 0;
    bool was_latched = false, first_outside_write = false;
    long under_latch = 0, outside = 0, low_repeats = 0, clearing_step = 0;
    uint32_t first_outside = 0, first_outside_pc = 0, clearing_write = 0, clearing_pc = 0;
    void internal(uint32_t addr) override {
        i_cycles++;
        AesMemory::internal(addr);
    }
    uint32_t read_word(uint32_t addr, bool seq) override {
        before(addr, false);
        const uint32_t v = AesMemory::read_word(addr, seq);
        count(addr, seq);
        note(addr, false, v);
        return v;
    }
    uint32_t read_byte(uint32_t addr, bool seq) override {
        before(addr, false);
        const uint32_t v = AesMemory::read_byte(addr, seq);
        count(addr, seq);
        note(addr, false, v);
        return v;
    }
    void write_word(uint32_t addr, uint32_t value, bool seq) override {
        before(addr, true);
        AesMemory::write_word(addr, value, seq);
        after_write(addr);
        count(addr, seq);
        note(addr, true, value);
    }
    void write_byte(uint32_t addr, uint8_t value, bool seq) override {
        before(addr, true);
        AesMemory::write_byte(addr, value, seq);
        after_write(addr);
        count(addr, seq);
        note(addr, true, value);
    }

private:
    void before(uint32_t addr, bool write) {
        was_latched = boot_latch_set();
        if (!was_latched) return;
        under_latch++;
        const uint32_t a = addr & 0x3FFFFFF;
        if (a >= 0x4000 && a < 0x1000000) low_repeats++;
        if (outside_strict_map(a)) {
            if (outside == 0) {
                first_outside = a;
                first_outside_pc = pc;
                first_outside_write = write;
            }
            outside++;
        }
    }
    void after_write(uint32_t addr) {
        if (was_latched && !boot_latch_set()) {
            clearing_write = addr;
            clearing_pc = pc;
            clearing_step = step;
        }
    }
    void count(uint32_t addr, bool seq) {
        (seq ? s_cycles : n_cycles)++;
        if (first_tube_address == 0 && (addr >> 24) == 1) first_tube_address = addr;
    }
    void note(uint32_t addr, bool write, uint32_t value) {
        if (abort_line) aborts.push_back({step, addr, value, write, pc});
    }
};

static uint32_t word_at(const AesMemory& mem, uint32_t addr) {
    return mem.peek(addr) | mem.peek(addr + 1) << 8 | mem.peek(addr + 2) << 16 |
           uint32_t(mem.peek(addr + 3)) << 24;
}

static const char* absent_name(Absent absent) { return absent == Absent::LastBus ? "last-bus" : "all-ones"; }

static AesOptions options(int mb, Abort setting, const char* rom, Absent absent) {
    AesOptions o;
    o.ram_absent_bank = absent;
    o.ram = mb;
    o.abort = setting;
    o.rom = rom;
    return o;
}

static void first_access_table(const char* rom, Absent absent) {
    printf("\nram.absent_bank %s\n", absent_name(absent));
    puts("RAM  abort  reached  instructions  cycles (S + N + I)                  ticks        ms  first access          instruction  R0  RAM found");
    for (int mb : {2, 4}) {
        for (Abort setting : {Abort::Off, Abort::Strict}) {
            const AesOptions o = options(mb, setting, rom, absent);
            AesTiming timing(o);
            QuietHost tube;
            WatchedMemory mem(o, timing, tube);
            mem.load_rom();
            ArmCore cpu(mem, o.cpu);
            long steps = 0;
            while (!tube.seen && steps < kFirstAccessLimit) {
                mem.pc = (cpu.r15() & kPcMask) - 8;
                cpu.step();
                steps++;
            }
            const long cycles = mem.s_cycles + mem.n_cycles + mem.i_cycles;
            printf("%d MB %-5s  %-7s %12ld  %8ld = %ld + %ld + %ld %12llu %9.3f  %-5s &%07X reg %d  &%07X  %2u  %uK\n", mb,
                   setting == Abort::Off ? "off" : "strict", tube.seen ? "yes" : "NO", steps, cycles,
                   mem.s_cycles, mem.n_cycles, mem.i_cycles, (unsigned long long)timing.ticks(),
                   double(timing.ticks()) * 50e-6, tube.was_write ? "write" : "read", mem.first_tube_address,
                   tube.reg, mem.pc, cpu.reg_in_mode(cpu.r15() & 3, 0), word_at(mem, kStrAcc) / 1024);
        }
    }
}

static void strict_run(int mb, const char* rom, Absent absent) {
    const AesOptions o = options(mb, Abort::Strict, rom, absent);
    AesTiming timing(o);
    QuietHost tube;
    WatchedMemory mem(o, timing, tube);
    mem.load_rom();
    ArmCore cpu(mem, o.cpu);
    uint32_t pcs[64] = {};
    for (long i = 0; i < kStrictLimit; i++) {
        mem.step = i;
        mem.pc = (cpu.r15() & kPcMask) - 8;
        pcs[i % 64] = mem.pc;
        cpu.step();
    }
    printf("\n=== strict, %d MB, %s: %ld steps, %zu aborts, %ld Tube accesses\n", mb, absent_name(absent),
           kStrictLimit, mem.aborts.size(), tube.accesses);
    printf("while the latch was set: %ld accesses, %ld outside the strict map, %ld at low addresses from &4000 up\n",
           mem.under_latch, mem.outside, mem.low_repeats);
    if (mem.outside) {
        printf("first outside: %s &%07X at &%07X\n", mem.first_outside_write ? "write" : "read",
               mem.first_outside, mem.first_outside_pc);
    }
    printf("latch cleared by the write to &%07X, instruction at &%07X = %08X, instruction number %ld\n",
           mem.clearing_write, mem.clearing_pc, word_at(mem, mem.clearing_pc), mem.clearing_step);
    size_t shown = 0;
    for (const AbortRecord& a : mem.aborts) {
        if (shown++ >= 12) break;
        printf("abort %2zu step %7ld  %s &%07X  instruction at &%07X = %08X  value %08X\n", shown, a.step,
               a.write ? "write" : "read ", a.address & kPcMask, a.pc, word_at(mem, a.pc), a.value);
    }
    if (!mem.aborts.empty()) {
        const AbortRecord& a = mem.aborts.back();
        printf("last abort step %ld %s &%07X at &%07X\n", a.step, a.write ? "write" : "read", a.address, a.pc);
    }
    printf("printed: [");
    for (char c : tube.text) {
        if (c >= 32 && c < 127) putchar(c);
        else printf("<%02X>", (unsigned char)c);
    }
    puts("]");
    const uint32_t mode = cpu.r15() & 3;
    printf("end: r15 %08X mode %u\n", cpu.r15(), mode);
    uint32_t low = 0xFFFFFFFF, high = 0;
    for (uint32_t pc : pcs) {
        low = pc < low ? pc : low;
        high = pc > high ? pc : high;
    }
    printf("last 64 instructions within &%07X-&%07X\n", low, high);
    printf("regs:");
    for (int r = 0; r < 15; r++) printf(" R%d=%X", r, cpu.reg_in_mode(mode, r));
    printf("\nstack from &%X:", cpu.reg_in_mode(3, 13));
    for (uint32_t at = cpu.reg_in_mode(3, 13); at < 0xE00; at += 4) printf(" %X", word_at(mem, at));
    printf("stored RAM size at &F00: %X\n", word_at(mem, kStrAcc));
}

int main(int argc, char** argv) {
    if (argc != 2) {
        puts("usage: milestone <rom file>");
        return 2;
    }
    {
        const AesOptions o = options(4, Abort::Off, argv[1], Absent::AllOnes);
        AesTiming timing(o);
        QuietHost tube;
        AesMemory mem(o, timing, tube);
        if (!mem.load_rom()) {
            printf("cannot load %s\n", argv[1]);
            return 2;
        }
    }
    for (Absent absent : {Absent::AllOnes, Absent::LastBus}) {
        first_access_table(argv[1], absent);
        for (int mb : {2, 4}) strict_run(mb, argv[1], absent);
    }
    return 0;
}

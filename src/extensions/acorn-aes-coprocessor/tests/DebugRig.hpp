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
// DebugRig.hpp, the rig the debugger's tests share
// version 2.0.0, 20 November 2026.

// AesDebug, Beebium's debugger target, over an AesTube driven as Beebium drives it, with
// Beebium's Tube chip counting the ARM's accesses. Shared by test_aes_debug.cpp and
// test_aes_debug_stops.cpp (split at 500 lines, step 4.4b).

#pragma once

#include "../beebium/AesDebug.hpp"
#include "ArmTest.hpp"

#include <beebium/tube/TubeUla.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

const uint32_t TUBE = 0x1000000, ROM = 0x3000000, REGION10 = 0x2000000;
const int H_R1STAT = 0, H_R1DATA = 1;  // The host's R1 registers (DOC, AN004 pp.11-13).
const uint8_t SET_P = 0x80 | 0x20, CLEAR_P = 0x20;  // Status writes: bit 7 sets the named flags.
const uint32_t MODE_BITS = 3, FLAGS = 0xFC000000;

inline std::vector<uint8_t> rom_of(std::initializer_list<uint32_t> words) {
    std::vector<uint8_t> rom(0x4000);
    size_t at = 0;
    for (uint32_t w : words) {
        for (int i = 0; i < 4; i++) rom[at++] = uint8_t(w >> (8 * i));
    }
    return rom;
}

// Counts every access the ARM makes to the Tube chip.
struct CountedUla : beebium::TubeUla {
    long accesses = 0;
    uint8_t coprocessor_read(uint8_t offset) override {
        accesses++;
        return TubeUla::coprocessor_read(offset);
    }
    void coprocessor_write(uint8_t offset, uint8_t value) override {
        accesses++;
        TubeUla::coprocessor_write(offset, value);
    }
};

struct DebugRig {
    CountedUla ula;
    AesOptions options;
    AesTube tube;
    AesDebug debug;
    uint64_t now = 1000;

    explicit DebugRig(const std::vector<uint8_t>& rom, AesOptions o = AesOptions())
        : options(o), tube(options, ula), debug(tube) {
        tube.set_rom(rom);
        tube.run_until(now);
    }
    void run(uint64_t host_cycles) {
        now += host_cycles;
        tube.run_until(now);
    }
    ArmCore& core() { return tube.core(); }
    size_t index_of(const std::string& name) {
        const auto& regs = debug.cpu_descriptor().registers;
        for (size_t i = 0; i < regs.size(); i++) {
            if (regs[i].name == name) return i;
        }
        return regs.size();
    }
    uint32_t value(const std::string& name) { return uint32_t(debug.register_value(index_of(name))); }
    void set(const std::string& name, uint32_t v) { debug.set_register_value(index_of(name), v); }
    // Into a mode with interrupts off, as TEQP would leave it (DOC, SW p.1).
    void enter_mode(uint32_t mode) { core().set_r15((core().r15() & ~MODE_BITS) | 0x0C000000 | mode); }
};

// Every register the debugger lists, with the core register it must show (DOC, SW p.1; the
// banks as the ARM1 has them: FIQ R10-R14, IRQ and SVC R13-R14).
struct Named {
    std::string name;
    int mode;  // -1: as the current mode sees it.
    int n;
};
inline std::vector<Named> every_banked_register() {
    std::vector<Named> list;
    for (int n = 0; n < 15; n++) list.push_back({"R" + std::to_string(n), -1, n});
    const char* suffix[] = {"usr", "fiq", "irq", "svc"};
    for (int mode = 0; mode < 4; mode++) {
        for (int n = mode == 0 || mode == 1 ? 10 : 13; n < 15; n++) {
            list.push_back({"R" + std::to_string(n) + "_" + suffix[mode], mode, n});
        }
    }
    return list;
}

// A loop of different instructions: R1 counts, R2 sums, a branch back.
inline std::vector<uint8_t> loop_rom() {
    return rom_of({dp_imm(MOV, false, 1, 0, 5), dp_imm(ADD, false, 1, 1, 1), dp_reg(ADD, false, 2, 2, 1),
                   dp_reg(MOV, false, 3, 0, 2, LSL, 2), branch(16, 4)});
}

inline bool same_registers(ArmCore& a, ArmCore& b) {
    bool same = a.r15() == b.r15();
    for (int mode = 0; mode < 4; mode++) {
        for (int n = 0; n < 15; n++) same = same && a.reg_in_mode(mode, n) == b.reg_in_mode(mode, n);
    }
    return same;
}

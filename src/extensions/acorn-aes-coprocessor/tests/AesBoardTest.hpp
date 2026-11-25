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
// AesBoardTest.hpp, a test rig: the core on the real memory map
// version 2.0.0, 20 November 2026.

// What the board tests share: a fake Tube, a known ROM and one small board.

#pragma once

#include "../AesMemory.hpp"
#include "ArmTest.hpp"

using M = AesOptions;
static const uint32_t ROM = 0x3000000, TUBE = 0x1000000, REGION10 = 0x2000000;
static const uint32_t MB = 0x100000;

struct FakeTube : TubePort {
    int reads = 0, writes = 0, last_reg = -1;
    uint8_t last_value = 0;
    uint8_t read(int reg) override {
        reads++;
        last_reg = reg;
        return 0xA0 + reg;
    }
    void write(int reg, uint8_t value) override {
        writes++;
        last_reg = reg;
        last_value = value;
    }
};

// Each ROM byte is a known function of its offset, so a wrong repeat or order shows.
inline std::vector<uint8_t> test_rom() {
    std::vector<uint8_t> rom(0x4000);
    for (size_t i = 0; i < rom.size(); i++) rom[i] = uint8_t(i ^ (i >> 8));
    return rom;
}

struct Board {
    AesOptions o;
    AesTiming timing;
    FakeTube tube;
    AesMemory mem;
    explicit Board(AesOptions options = AesOptions()) : o(options), timing(o), mem(o, timing, tube) {
        mem.set_rom(test_rom());
    }
    void clear_latch() { mem.write_word(0x8000, 0, false); }
    bool abort_after_read(uint32_t addr) {
        mem.read_word(addr, false);
        const bool aborted = mem.abort_line;
        mem.abort_line = false;
        return aborted;
    }
};

inline AesOptions with_ram(int mb) {
    AesOptions o;
    o.ram = mb;
    return o;
}

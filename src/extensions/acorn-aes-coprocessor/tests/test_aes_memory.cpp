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
// test_aes_memory.cpp, tests the memory map
// version 2.0.0, 20 November 2026.

// The memory map: boot latch, RAM banks, ROM, region 10, peek.

#include "AesBoardTest.hpp"

#include <filesystem>
#include <fstream>

TEST(mem_defaults, "ram 4 MB, abort off, no default rom (USER, Plugin options 2-4; owner, 17 Nov 2026)") {
    AesOptions o;
    CHECK_EQ(o.ram, 4);
    CHECK_EQ(o.abort == M::MachineAbort::Off, true);
    CHECK_EQ(o.rom.empty(), true);
}

TEST(mem_bad_ram_size, "ram takes 2 or 4 MB only (USER, Plugin option 3; 1 MB removed, USER, 5 Nov 2026)") {
    for (int mb : {1, 3}) {
        AesOptions o;
        o.ram = mb;
        AesTiming timing(o);
        FakeTube tube;
        bool refused = false;
        try {
            AesMemory mem(o, timing, tube);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        CHECK_EQ(refused, true);
    }
}

TEST(mem_latch_low_reads_rom, "boot.latch: until the first write, low addresses read the ROM (DOC, HW pp.34-35)") {
    Board b;
    CHECK_EQ(b.mem.read_word(0, false), 0x03020100);
    CHECK_EQ(b.mem.read_word(0x1000, false), b.mem.read_word(ROM + 0x1000, false));
}

TEST(mem_latch_first_write, "boot.latch: the first write clears it (DOC, HW p.35); it lands in RAM here, a stand-in (UNK, 7a)") {
    Board b;
    b.mem.write_word(0x10, 0x12345678, false);
    CHECK_EQ(b.mem.read_word(0x10, false), 0x12345678);
    CHECK_EQ(b.mem.read_word(0, false), 0);
}

TEST(mem_latch_any_write, "boot.latch: a Tube or ROM write clears it too (DOC, HW p.35: no address term); RAM-only what-if") {
    for (uint32_t addr : {TUBE, ROM}) {
        Board b;
        b.mem.write_byte(addr, 1, false);
        CHECK_EQ(b.mem.read_word(0, false), 0);
        AesOptions o;
        o.boot_latch = M::BootLatch::RamWritesOnly;
        Board c(o);
        c.mem.write_byte(addr, 1, false);
        CHECK_EQ(c.mem.read_word(0, false), 0x03020100);
        c.mem.write_word(0x8000, 1, false);
        CHECK_EQ(c.mem.read_word(0, false), 0);
    }
}

TEST(mem_reset_keeps_ram, "reset sets the latch again and RAM survives (DOC, HW p.35; INF, profile ram.power_on)") {
    Board b;
    b.mem.write_word(0x8000, 0x55, false);
    b.mem.reset();
    CHECK_EQ(b.mem.read_word(0, false), 0x03020100);
    b.clear_latch();
    b.mem.write_word(0x8004, 0x66, false);
    b.mem.reset();
    b.mem.write_word(0x8008, 0x77, false);
    CHECK_EQ(b.mem.read_word(0x8004, false), 0x66);
}

TEST(mem_power_on, "ram.power_on: zero by default, random alternative; contents reset by power-on (INF, USER)") {
    Board b;
    b.clear_latch();
    b.mem.write_word(0x9000, 0x55, false);
    b.mem.power_on();
    b.clear_latch();
    CHECK_EQ(b.mem.read_word(0x9000, false), 0);
    AesOptions o;
    o.ram_power_on = M::RamPowerOn::Random;
    Board c(o);
    c.mem.power_on();
    c.clear_latch();
    int non_zero = 0;
    for (uint32_t a = 0x9000; a < 0x9040; a += 4) non_zero += c.mem.read_word(a, false) != 0;
    CHECK_EQ(non_zero > 8, true);
}

TEST(mem_rom_word_and_byte, "ROM: bytes build a little-endian word; a byte read gives its own byte (DOC, HW p.34; SRC, reset vector)") {
    Board b;
    CHECK_EQ(b.mem.read_word(ROM + 0x20, false), 0x23222120);
    CHECK_EQ(b.mem.read_byte(ROM + 0x21, false), 0x21);
}

TEST(mem_rom_repeat, "The ROM repeats every &4000 through region 11 (INF from DOC: ROM has A2-A13 only, RCS only A24, A25; the real AES, test 4 part 1)") {
    Board b;
    CHECK_EQ(b.mem.read_word(ROM + 0x4020, false), 0x23222120);
    CHECK_EQ(b.mem.read_word(0x3FFC020, false), 0x23222120);
}

TEST(mem_rom_write_ignored, "rom.write: ignored (INF, latch equation)") {
    Board b;
    const uint32_t before = b.mem.read_word(ROM + 0x20, false);
    b.mem.write_word(ROM + 0x20, 0xDEADBEEF, false);
    CHECK_EQ(b.mem.read_word(ROM + 0x20, false), before);
}

TEST(mem_rom_load, "rom: a file is read at run time; a missing or wrong-sized file is refused (USER, Plugin option 4; DOC, 16 KB 27128)") {
    const std::string dir = std::filesystem::temp_directory_path().string() + "/";
    const std::string good = dir + "good.rom", small = dir + "small.rom";
    std::ofstream(good, std::ios::binary).write((const char*)test_rom().data(), 0x4000);
    std::ofstream(small, std::ios::binary).write("short", 5);
    AesOptions o;
    AesTiming timing(o);
    FakeTube tube;
    AesMemory mem(o, timing, tube);
    CHECK_EQ(mem.load_rom(good), true);
    CHECK_EQ(mem.read_word(ROM + 0x20, false), 0x23222120);
    CHECK_EQ(mem.load_rom(small), false);
    CHECK_EQ(mem.load_rom(dir + "no-such-file"), false);
}

TEST(mem_ram_sizes, "ram: fitted banks answer, each 1 MB, chosen by A20, A21 (DOC, HW 5.1.2; USER)") {
    for (int mb : {2, 4}) {
        Board b(with_ram(mb));
        b.clear_latch();
        for (uint32_t bank = 0; bank < 4; bank++) {
            const uint32_t addr = bank * MB + 0x100;
            b.mem.write_word(addr, 0x1000 + bank, false);
            CHECK_EQ(b.mem.read_word(addr, false), int(bank) < mb ? 0x1000 + bank : 0xFFFFFFFF);
        }
    }
}

TEST(mem_ram_repeat, "ram.repeat: A22, A23 ignored, so a 4 MB repeat at any size (INF, HW 5.1.2); modulo-fitted alternative") {
    for (int mb : {2, 4}) {
        Board b(with_ram(mb));
        b.clear_latch();
        b.mem.write_word(0x100, 0x77, false);
        CHECK_EQ(b.mem.read_word(4 * MB + 0x100, false), 0x77);
        CHECK_EQ(b.mem.read_word(12 * MB + 0x100, false), 0x77);
    }
    AesOptions o = with_ram(2);
    o.ram_repeat = M::RamRepeat::FittedSize;
    Board c(o);
    c.clear_latch();
    c.mem.write_word(0x100, 0x77, false);
    CHECK_EQ(c.mem.read_word(2 * MB + 0x100, false), 0x77);
}

TEST(mem_ram_bytes, "RAM: a byte write changes one byte, little-endian (DOC, SW p.19)") {
    Board b;
    b.clear_latch();
    b.mem.write_word(0x100, 0x11223344, false);
    b.mem.write_byte(0x101, 0xAB, false);
    CHECK_EQ(b.mem.read_word(0x100, false), 0x1122AB44);
    CHECK_EQ(b.mem.read_byte(0x103, false), 0x11);
}

TEST(mem_absent_bank, "ram.absent_bank: reads &FFFFFFFF, an arbitrary value, never what was written (INF that nothing answers; value UNK, no document); last-bus and low-RAM alternatives") {
    Board b(with_ram(2));
    b.clear_latch();
    b.mem.write_word(2 * MB, 0x12345678, false);
    CHECK_EQ(b.mem.read_word(2 * MB, false), 0xFFFFFFFF);
    CHECK_EQ(b.mem.read_byte(2 * MB + 1, false), 0xFF);
    AesOptions o = with_ram(2);
    o.ram_absent_bank = M::RamAbsentBank::LastBus;
    Board c(o);
    c.clear_latch();
    c.mem.write_word(0x100, 0x5A5A1234, false);
    c.mem.read_word(0x100, false);
    CHECK_EQ(c.mem.read_word(2 * MB, false), 0x5A5A1234);
    o.ram_absent_bank = M::RamAbsentBank::LowRam;
    Board d(o);
    d.clear_latch();
    d.mem.write_word(0x100, 0x99, false);
    CHECK_EQ(d.mem.read_word(2 * MB + 0x100, false), 0x99);
}

TEST(mem_absent_write_discarded, "ram.absent_write: discarded, low RAM untouched (INF)") {
    Board b(with_ram(2));
    b.clear_latch();
    b.mem.write_word(2 * MB + 0x100, 0x99, false);
    CHECK_EQ(b.mem.read_word(0x100, false), 0);
}

TEST(mem_absent_write_low_ram_alt, "ram.absent_write: discarded even when reads repeat low RAM (INF)") {
    AesOptions o = with_ram(2);
    o.ram_absent_bank = M::RamAbsentBank::LowRam;
    Board b(o);
    b.clear_latch();
    b.mem.write_word(2 * MB + 0x100, 0x99, false);
    CHECK_EQ(b.mem.read_word(0x100, false), 0);
}

TEST(mem_last_bus, "ram.absent_bank last bus: every read and write counts, Tube bytes included; a byte write is on all four lanes (DOC, HW 3.4); UNK that the bus holds it") {
    AesOptions o = with_ram(2);
    o.ram_absent_bank = M::RamAbsentBank::LastBus;
    Board b(o);
    b.clear_latch();
    b.mem.write_word(0x100, 0x12345678, false);
    CHECK_EQ(b.mem.read_word(2 * MB, false), 0x12345678);
    b.mem.write_byte(0x101, 0xAB, false);
    CHECK_EQ(b.mem.read_word(2 * MB, false), 0xABABABAB);
    b.mem.read_byte(TUBE + 8, false);
    CHECK_EQ(b.mem.read_word(2 * MB, false), 0xABABABA2);
    CHECK_EQ(b.mem.read_word(REGION10, false), 0xABABABA2);
}

TEST(mem_region10_default, "region10.read and .write: nothing answers, same value as absent RAM (INF from DOC: RAS and CAS need /A24*/A25, HW 5.1.4; value UNK)") {
    Board b;
    b.clear_latch();
    b.mem.write_word(REGION10 + 0x100, 0x99, false);
    CHECK_EQ(b.mem.read_word(REGION10 + 0x100, false), 0xFFFFFFFF);
    CHECK_EQ(b.mem.read_word(0x100, false), 0);
}

TEST(mem_region10_alternatives, "region10: abort and RAM-repeat what-ifs (UTIL 2.3, Brazil p.4); abort needs abort strict (USER, 31 Oct 2026; empty removed, USER, 5 Nov 2026)") {
    AesOptions o;
    o.abort = M::MachineAbort::Strict;  // Before user mode, so its own map is not checked (USER, 4 Nov 2026).
    o.region10_read = M::Region10Read::Abort;
    Board b(o);
    CHECK_EQ(b.abort_after_read(REGION10), true);
    b.mem.write_word(REGION10, 0, false);
    CHECK_EQ(b.mem.abort_line, false);
    AesOptions p;
    p.region10_read = M::Region10Read::RamRepeat;
    p.region10_write = M::Region10Write::RamRepeat;
    Board c(p);
    c.clear_latch();
    c.mem.write_word(REGION10 + 0x100, 0x99, false);
    CHECK_EQ(c.mem.read_word(0x100, false), 0x99);
    CHECK_EQ(c.mem.read_word(REGION10 + 0x100, false), 0x99);
}

TEST(mem_peek, "peek changes nothing: no latch change, no Tube access, no time (design, notes item 28)") {
    Board b;
    const uint64_t before = b.timing.ticks();
    CHECK_EQ(b.mem.peek(ROM + 0x21), 0x21);
    CHECK_EQ(b.mem.peek(0x21), 0x21);
    b.mem.peek(TUBE);
    CHECK_EQ(b.tube.reads, 0);
    CHECK_EQ(b.timing.ticks(), before);
}

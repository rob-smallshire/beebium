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
// test_aes_tube_abort.cpp, tests aborts and access checks at the Tube
// version 2.0.0, 20 November 2026.

// The Tube addresses, ABORT, and how the map's cycles reach the timing.

#include "AesBoardTest.hpp"

TEST(mem_tube_registers, "Tube: eight registers a word apart, A2-A4 select, byte access (SRC, Kernel:101-108; DOC, HW 5.3)") {
    Board b;
    CHECK_EQ(b.mem.read_byte(TUBE + 8, false), 0xA2);
    CHECK_EQ(b.tube.last_reg, 2);
    b.mem.write_byte(TUBE + 28, 0x5C, false);
    CHECK_EQ(b.tube.last_reg, 7);
    CHECK_EQ(b.tube.last_value, 0x5C);
}

TEST(mem_tube_repeat, "tube.addr_decode: repeats every 32 bytes in the lower half (INF, HW 5.3); nothing above; whole-region alternative") {
    Board b;
    CHECK_EQ(b.mem.read_byte(TUBE + 0x7FFFE8, false), 0xA2);
    b.tube.reads = 0;
    CHECK_EQ(b.mem.read_byte(TUBE + 0x800008, false), 0xFF);
    CHECK_EQ(b.tube.reads, 0);
    AesOptions o;
    o.tube_addr_decode = M::TubeAddrDecode::WholeRegion;
    Board c(o);
    CHECK_EQ(c.mem.read_byte(TUBE + 0x800008, false), 0xA2);
}

TEST(mem_tube_width, "tube.width: a word access is the low byte on D0-D7 (DOC, sheet), the other lanes undriven (value UNK); undefined-data alternative, reads and writes (UNK)") {
    Board b;
    CHECK_EQ(b.mem.read_word(TUBE + 8, false), 0xFFFFFFA2);
    b.mem.write_word(TUBE + 4, 0x11223344, false);
    CHECK_EQ(b.tube.last_value, 0x44);
    AesOptions o;
    o.tube_width = M::TubeWidth::Undefined;
    Board c(o);
    CHECK_EQ(c.mem.read_word(TUBE + 8, false), 0xFFFFFFFF);
    CHECK_EQ(c.tube.reads, 1);
    c.mem.write_word(TUBE + 4, 0x11223344, false);
    CHECK_EQ(c.tube.last_value, 0xFF);
    c.mem.write_byte(TUBE + 4, 0x44, false);
    CHECK_EQ(c.tube.last_value, 0x44);
}

TEST(mem_tube_byte_lanes, "Tube byte read with A0 or A1 set: the register is reached (INF, 7a: A2-A4 only) but the byte comes from an undriven lane (DOC, sheet D0-D7, HW 3.4; value UNK)") {
    for (uint32_t lane : {1u, 2u, 3u}) {
        Board b;
        CHECK_EQ(b.mem.read_byte(TUBE + 8 + lane, false), 0xFF);
        CHECK_EQ(b.tube.reads, 1);
        CHECK_EQ(b.tube.last_reg, 2);
    }
    AesOptions o;
    o.ram_absent_bank = M::RamAbsentBank::LastBus;
    Board c(o);
    c.clear_latch();
    c.mem.write_word(0x100, 0x11223344, false);
    CHECK_EQ(c.mem.read_byte(TUBE + 9, false), 0x33);
    CHECK_EQ(c.mem.read_word(TUBE + 8, false), 0x112233A2);
}

TEST(mem_abort_off, "abort off: nothing ever aborts, as ABORT tied low (USER, Plugin option 2; DOC low-res)") {
    Board b(with_ram(2));
    b.clear_latch();
    for (uint32_t addr : {0x100u, 2 * MB, REGION10, TUBE + 0x800000, TUBE}) {
        CHECK_EQ(b.abort_after_read(addr), false);
    }
}

TEST(mem_abort_off_wins, "abort off: no ABORT whatever else is set; region10.read=Abort and access.check=Abort act only when it is not off (USER, 31 Oct 2026)") {
    AesOptions o;
    o.region10_read = M::Region10Read::Abort;
    o.access_check = M::MachineCheck::Abort;
    Board b(o);
    CHECK_EQ(b.abort_after_read(REGION10), false);
    b.mem.write_word(ROM, 0, false);
    CHECK_EQ(b.mem.abort_line, false);
    CHECK_EQ(b.abort_after_read(TUBE), false);
    CHECK_EQ(b.mem.suspect_accesses(), 2);
    o.abort = M::MachineAbort::Strict;  // Before user mode, so its own map is not checked (USER, 4 Nov 2026).
    Board c(o);
    CHECK_EQ(c.abort_after_read(REGION10), true);
    CHECK_EQ(c.abort_after_read(TUBE), true);
}

static Board* strict_board(int mb) {
    AesOptions o = with_ram(mb);
    o.abort = M::MachineAbort::Strict;
    return new Board(o);
}

TEST(mem_strict_installed, "abort strict: installed RAM, the Tube's 32 bytes and the 16 KB ROM do not abort (USER, 31 Oct 2026)") {
    for (int mb : {2, 4}) {
        Board* b = strict_board(mb);
        b->clear_latch();
        b->mem.user_mode = true;
        CHECK_EQ(b->mem.abort_line, false);
        for (uint32_t addr : {0u, uint32_t(mb) * MB - 4, TUBE, TUBE + 28, ROM, ROM + 0x3FFC}) {
            CHECK_EQ(b->abort_after_read(addr), false);
        }
        delete b;
    }
}

TEST(mem_strict_outside, "abort strict: anything outside them aborts, repeats and region 10 included (USER, 31 Oct 2026)") {
    for (int mb : {2, 4}) {
        Board* b = strict_board(mb);
        b->clear_latch();
        b->mem.user_mode = true;
        const uint32_t ram_top = uint32_t(mb) * MB;
        for (uint32_t addr : {ram_top, 4 * MB + 0x100, 0xFFFFFCu, TUBE + 32, TUBE + 0x7FFFE8, TUBE + 0x800000,
                              REGION10, ROM + 0x4000, 0x3FFC020u}) {
            CHECK_EQ(b->abort_after_read(addr), true);
        }
        delete b;
    }
}

TEST(mem_strict_repeats_empty, "abort strict: a repeat is empty: no Tube access, no value, no store (USER, 31 Oct 2026; INF, 7a that the store stops)") {
    Board* b = strict_board(4);
    b->clear_latch();
    b->mem.user_mode = true;
    b->tube.reads = 0;
    CHECK_EQ(b->mem.read_byte(TUBE + 0x20, false), 0xFF);
    CHECK_EQ(b->tube.reads, 0);
    b->mem.abort_line = false;
    b->mem.write_word(TUBE + 0x24, 1, false);
    CHECK_EQ(b->tube.writes, 0);
    b->mem.abort_line = false;
    b->mem.write_word(0x100, 0x55, false);
    b->mem.write_word(4 * MB + 0x100, 0x66, false);
    CHECK_EQ(b->mem.abort_line, true);
    b->mem.abort_line = false;
    CHECK_EQ(b->mem.read_word(0x100, false), 0x55);
    CHECK_EQ(b->mem.read_word(ROM + 0x4020, false), 0xFFFFFFFF);
    delete b;
}

TEST(mem_strict_boot, "abort strict: low addresses under the latch are installed RAM addresses, so the reset fetch does not abort (USER; DOC, HW p.34)") {
    Board* b = strict_board(2);
    CHECK_EQ(b->abort_after_read(0), false);
    CHECK_EQ(b->mem.read_word(0, false), 0x03020100);
    delete b;
}

TEST(mem_strict_latch_window, "abort strict: while the boot latch is set every access behaves as with abort off (USER, 31 Oct 2026)") {
    for (int mb : {2, 4}) {
        Board* strict = strict_board(mb);
        Board off(with_ram(mb));
        for (uint32_t addr : {0x100u, 0x4100u, 4 * MB + 0x100, TUBE + 32, TUBE + 0x800000, REGION10, ROM + 0x4020, 0x3FFC020u}) {
            CHECK_EQ(strict->abort_after_read(addr), false);
            off.mem.read_word(addr, false);
            CHECK_EQ(strict->mem.read_word(addr, false), off.mem.read_word(addr, false));
        }
        CHECK_EQ(strict->tube.reads, off.tube.reads);
        delete strict;
    }
}

TEST(mem_strict_clearing_write, "abort strict: the write that clears the latch is still in the window, and behaves as with abort off (USER, 31 Oct 2026)") {
    Board* b = strict_board(2);
    b->mem.write_word(4 * MB + 0x100, 0x66, false);
    CHECK_EQ(b->mem.abort_line, false);
    CHECK_EQ(b->mem.read_word(0x100, false), 0x66);
    delete b;
    Board* t = strict_board(2);
    t->mem.write_byte(TUBE + 0x24, 0x77, false);
    CHECK_EQ(t->mem.abort_line, false);
    CHECK_EQ(t->tube.writes, 1);
    delete t;
}

TEST(mem_strict_lax_before_user, "abort strict: after the latch clears, accesses in a privileged mode behave as with abort off, so the Executive's RAM probe passes (USER, 4 Nov 2026)") {
    for (int mb : {2, 4}) {
        Board* strict = strict_board(mb);
        Board off(with_ram(mb));
        strict->clear_latch();
        off.clear_latch();
        for (uint32_t addr : {uint32_t(mb) * MB, 4 * MB + 0x100, TUBE + 32, TUBE + 0x800000, REGION10, ROM + 0x4020, 0x3FFC020u}) {
            CHECK_EQ(strict->abort_after_read(addr), false);
            off.abort_after_read(addr);
            CHECK_EQ(strict->mem.read_word(addr, false), off.mem.read_word(addr, false));
        }
        strict->mem.write_word(4 * MB + 0x100, 0x66, false);
        off.mem.write_word(4 * MB + 0x100, 0x66, false);
        CHECK_EQ(strict->mem.abort_line, false);
        CHECK_EQ(strict->mem.read_word(0x100, false), off.mem.read_word(0x100, false));
        CHECK_EQ(strict->tube.reads, off.tube.reads);
        CHECK_EQ(strict->mem.strict_on(), false);
        delete strict;
    }
}

TEST(mem_strict_starts_in_user, "abort strict: checking starts with the first access in user mode, and that access is itself checked (USER, 4 Nov 2026)") {
    Board* b = strict_board(2);
    b->clear_latch();
    b->mem.user_mode = true;
    CHECK_EQ(b->abort_after_read(REGION10), true);
    CHECK_EQ(b->mem.strict_on(), true);
    delete b;
    Board* c = strict_board(2);
    c->clear_latch();
    c->mem.user_mode = true;
    CHECK_EQ(c->abort_after_read(0x100), false);
    CHECK_EQ(c->mem.strict_on(), true);
    delete c;
}

TEST(mem_strict_stays_on, "abort strict: once on, it stays on in every mode until a reset, so a bad address in an SWI handler aborts (USER, 4 Nov 2026)") {
    Board* b = strict_board(2);
    b->clear_latch();
    b->mem.user_mode = true;
    CHECK_EQ(b->abort_after_read(0x100), false);
    b->mem.user_mode = false;
    CHECK_EQ(b->abort_after_read(REGION10), true);
    CHECK_EQ(b->abort_after_read(4 * MB + 0x100), true);
    b->mem.write_word(4 * MB + 0x100, 0x66, false);
    CHECK_EQ(b->mem.abort_line, true);
    delete b;
}

TEST(mem_strict_lax_after_reset, "abort strict: every reset puts it back to lax, so the probe after a Break passes; user mode starts it again (USER, 4 Nov 2026)") {
    Board* b = strict_board(2);
    b->clear_latch();
    b->mem.user_mode = true;
    CHECK_EQ(b->abort_after_read(REGION10), true);
    b->mem.reset();
    b->mem.user_mode = false;
    CHECK_EQ(b->mem.strict_on(), false);
    b->clear_latch();
    CHECK_EQ(b->abort_after_read(REGION10), false);
    CHECK_EQ(b->abort_after_read(4 * MB + 0x100), false);
    b->mem.user_mode = true;
    CHECK_EQ(b->abort_after_read(REGION10), true);
    delete b;
}

TEST(mem_check, "access.check: off, log or abort for ROM writes and Tube width (provisional design, Plugin option 5)") {
    Board b;
    b.mem.write_word(ROM, 0, false);
    CHECK_EQ(b.mem.suspect_accesses(), 0);
    AesOptions o;
    o.access_check = M::MachineCheck::Log;
    Board c(o);
    c.mem.write_word(ROM, 0, false);
    c.mem.read_word(TUBE, false);
    CHECK_EQ(c.mem.suspect_accesses(), 2);
    CHECK_EQ(c.mem.abort_line, false);
    o.access_check = M::MachineCheck::Abort;
    o.abort = M::MachineAbort::Strict;  // Before user mode, so its own map is not checked (USER, 4 Nov 2026).
    Board d(o);
    d.mem.write_word(ROM, 0, false);
    CHECK_EQ(d.mem.abort_line, true);
}

static AesOptions no_refresh() {
    AesOptions o;
    o.time_refresh_cost = 0;
    return o;
}

TEST(mem_timing_latch, "note_cycle: under the latch, A24 low waits as ROM, region 10 and a write too (DOC, IC18 'do not wait for RAM' needs the latch clear; INF that it costs the ROM wait)") {
    Board b(no_refresh());
    b.mem.read_word(0x1004, true);
    CHECK_EQ(b.timing.ticks(), 6 + 24);
    b.mem.read_word(REGION10 + 4, false);
    CHECK_EQ(b.timing.ticks(), 2 * (6 + 24));
    b.mem.write_word(0x1004, 0, false);
    CHECK_EQ(b.timing.ticks(), 3 * (6 + 24));
    b.mem.read_word(0x1008, false);
    CHECK_EQ(b.timing.ticks(), 3 * (6 + 24) + 6);
    b.mem.read_word(REGION10 + 8, false);
    CHECK_EQ(b.timing.ticks(), 3 * (6 + 24) + 12);
}

TEST(mem_timing_places, "note_cycle, latch clear: low and unfitted RAM as RAM, ROM with its wait, Tube with its wait, upper Tube half with its wait, region 10 as N") {
    AesOptions o = no_refresh();
    o.ram = 2;
    Board b(o);
    b.clear_latch();
    const auto cost = [&](uint32_t addr, bool seq) {
        const uint64_t before = b.timing.ticks();
        b.mem.read_word(addr, seq);
        return int(b.timing.ticks() - before);
    };
    CHECK_EQ(cost(0x1004, true), 3);
    CHECK_EQ(cost(MB + 0x1004, true), 3);
    CHECK_EQ(cost(ROM + 4, true), 6 + 24);
    CHECK_EQ(cost(TUBE + 4, true), 6 + 6);
    CHECK_EQ(cost(TUBE + 0x800004, true), 6 + 6);
    CHECK_EQ(cost(REGION10 + 4, true), 6);
}

TEST(mem_timing_internal, "internal cycles: the address decides, so RAM holds off a refresh, and under the latch low addresses do not (DOC, HW 4.3; INF, RT = RSTLATCH, item 13)") {
    AesOptions o;
    Board latched(o);
    for (int i = 0; i < 64; i++) latched.mem.internal(0x1004);
    CHECK_EQ(latched.timing.ticks(), 192 + 9);
    Board b(o);
    b.clear_latch();
    const uint64_t start = b.timing.ticks();
    for (int i = 0; i < 70; i++) b.mem.internal(0x1004);
    CHECK_EQ(b.timing.ticks(), start + 210);
    b.mem.internal(ROM + 4);
    CHECK_EQ(b.timing.ticks(), start + 213 + 9);
}

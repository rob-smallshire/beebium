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
// test_aes_timing.cpp, tests the board timing
// version 2.0.0, 20 November 2026.

// Time in crystal ticks: 50 ns each. Refresh is moved out of the way in most tests
// so that one cost is seen at a time.

#include "../AesTiming.hpp"
#include "ArmTest.hpp"

using M = AesOptions;
using Place = AesTiming::Place;

static AesOptions no_refresh() {
    AesOptions o;
    o.time_refresh_cost = 0;
    return o;
}

TEST(time_s_n_internal, "S 3 ticks, N 6, internal as S (DOC, SW p.33; SW p.32; 20 MHz, USER)") {
    AesOptions o = no_refresh();
    AesTiming t(o);
    t.memory_cycle(true, Place::Ram, 0x1004);
    CHECK_EQ(t.ticks(), 3);
    t.memory_cycle(false, Place::Ram, 0x1008);
    CHECK_EQ(t.ticks(), 9);
    t.internal_cycle(Place::Ram, 0x100C);
    CHECK_EQ(t.ticks(), 12);
}

TEST(time_rom_wait, "time.rom_wait: 8 PAL clocks of 3 ticks on an N cycle, sequential or not (INF from DOC, HW 5.1.4: no S path outside RAM; N+8 is a guess)") {
    AesOptions o = no_refresh();
    AesTiming t(o);
    t.memory_cycle(true, Place::Rom, 0x3000004);
    CHECK_EQ(t.ticks(), 6 + 24);
    o.time_rom_wait = 10;
    AesTiming u(o);
    u.memory_cycle(false, Place::Rom, 0x3000004);
    CHECK_EQ(u.ticks(), 6 + 30);
}

TEST(time_tube_wait, "time.tube_wait: 2 PAL clocks on an N cycle, sequential or not (INF, item 14; INF from DOC, HW 5.1.4)") {
    AesOptions o = no_refresh();
    AesTiming t(o);
    t.memory_cycle(false, Place::Tube, 0x1000000);
    CHECK_EQ(t.ticks(), 6 + 6);
    t.memory_cycle(true, Place::Tube, 0x1000004);
    CHECK_EQ(t.ticks(), 2 * (6 + 6));
}

TEST(time_tube_sync, "time.tube_sync: none by default (INF, HW 5.3); host-edge alternative, 10 ticks a host cycle") {
    AesOptions d = no_refresh();
    AesTiming plain(d);
    plain.memory_cycle(false, Place::Ram, 0x1000);
    plain.memory_cycle(false, Place::Tube, 0x1000000);
    CHECK_EQ(plain.ticks(), 18);
    AesOptions o = no_refresh();
    o.time_tube_sync = M::TubeSync::NextHostEdge;
    AesTiming t(o);
    t.memory_cycle(false, Place::Ram, 0x1000);
    t.memory_cycle(false, Place::Tube, 0x1000000);
    CHECK_EQ(t.ticks(), 20);
}

TEST(time_nothing_no_wait, "Region 10, latch clear: no wait (INF, IC18 'do not wait for RAM' covers A24 low); N even if sequential (INF from DOC, HW 5.1.4)") {
    AesOptions o = no_refresh();
    AesTiming t(o);
    t.memory_cycle(false, Place::Nothing, 0x2000000);
    CHECK_EQ(t.ticks(), 6);
    t.memory_cycle(true, Place::Nothing, 0x2000004);
    CHECK_EQ(t.ticks(), 12);
}

TEST(time_block_break, "time.block_break: a sequential RAM cycle crossing a 32-word block costs as N (INF from DOC, IC9); no-break alternative") {
    AesOptions o = no_refresh();
    AesTiming t(o);
    t.memory_cycle(true, Place::Ram, 0x107C);
    CHECK_EQ(t.ticks(), 3);
    t.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(t.ticks(), 9);
    o.time_block_break = M::BlockBreak::None;
    AesTiming u(o);
    u.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(u.ticks(), 3);
}

TEST(time_block_break_after_last_word, "time.block_break: a sequential RAM cycle is slow when the cycle before it, memory or internal, was at a block's last word (INF from DOC, IC9, HW 5.1.4; the real AES, test 17 EARLY and XLDR)") {
    AesOptions o = no_refresh();
    // EARLY: an internal cycle at X-4 (its address is the next fetch's), then the fetches of
    // X-4 and X: both slow.
    AesTiming early(o);
    early.internal_cycle(Place::Ram, 0x107C);
    early.memory_cycle(true, Place::Ram, 0x107C);
    early.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(early.ticks(), 3 + 6 + 6);
    // XLDR: a data cycle elsewhere, an internal cycle at X, then the fetch of X: fast.
    AesTiming xldr(o);
    xldr.memory_cycle(false, Place::Ram, 0x1700);
    xldr.internal_cycle(Place::Ram, 0x1080);
    xldr.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(xldr.ticks(), 6 + 3 + 3);
}

TEST(time_block_break_at_block_start, "time.block_break alternative: the old rule, the cycle's own address starts a block (INF, old OI-74)") {
    AesOptions o = no_refresh();
    o.time_block_break = M::BlockBreak::AtBlockStart;
    AesTiming early(o);
    early.internal_cycle(Place::Ram, 0x107C);
    early.memory_cycle(true, Place::Ram, 0x107C);
    early.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(early.ticks(), 3 + 3 + 6);
    AesTiming xldr(o);
    xldr.memory_cycle(false, Place::Ram, 0x1700);
    xldr.internal_cycle(Place::Ram, 0x1080);
    xldr.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(xldr.ticks(), 6 + 3 + 6);
}

TEST(time_block_is_32_words, "time.block_break: a 16-word boundary is not a block end (INF from DOC, IC9: A2-A6)") {
    AesOptions o = no_refresh();
    AesTiming t(o);
    t.memory_cycle(true, Place::Ram, 0x103C);
    t.memory_cycle(true, Place::Ram, 0x1040);
    CHECK_EQ(t.ticks(), 3 + 3);
}

TEST(time_refresh_cost, "time.refresh_period 192 ticks, time.refresh_cost 9: taken on the next non-sequential cycle (INF, item 12; INF from DOC, HW p.36; DOC, HW IC9)") {
    AesOptions o;
    AesTiming t(o);
    for (int i = 0; i < 64; i++) t.memory_cycle(true, Place::Ram, 0x1004);
    CHECK_EQ(t.ticks(), 192);
    t.memory_cycle(false, Place::Ram, 0x1008);
    CHECK_EQ(t.ticks(), 192 + 6 + 9);
    t.memory_cycle(false, Place::Ram, 0x100C);
    CHECK_EQ(t.ticks(), 192 + 6 + 9 + 6);
}

TEST(time_refresh_cost_default, "time.refresh_cost: 5 clocks CAS to CAS through a refresh, 3 more than N, so 9 ticks (INF from DOC, HW p.36)") {
    AesOptions o;
    CHECK_EQ(o.time_refresh_cost, 9);
}

TEST(time_refresh_waits_in_page, "time.refresh_cost: a pending refresh waits through sequential RAM cycles inside a block, and starts on the cycle after the block's last word, with the break (INF from DOC, HW 5.1.4, item 13; time.block_break)") {
    AesOptions o;
    AesTiming t(o);
    for (int i = 0; i < 70; i++) t.memory_cycle(true, Place::Ram, 0x1004);
    CHECK_EQ(t.ticks(), 210);
    t.memory_cycle(true, Place::Ram, 0x107C);
    CHECK_EQ(t.ticks(), 210 + 3);
    t.memory_cycle(true, Place::Ram, 0x1080);
    CHECK_EQ(t.ticks(), 210 + 3 + 6 + 9);
}

TEST(time_refresh_internal, "Refresh: an internal cycle in RAM holds SEQ high (DOC, HW 4.3), so it starts one only with A2-A6 all 1; elsewhere it can (INF from DOC, HW 5.1.4, RFADD)") {
    AesOptions o;
    AesTiming u(o);
    for (int i = 0; i < 70; i++) u.memory_cycle(true, Place::Ram, 0x1004);
    u.internal_cycle(Place::Ram, 0x1008);
    CHECK_EQ(u.ticks(), 210 + 3);
    u.internal_cycle(Place::Ram, 0x107C);
    CHECK_EQ(u.ticks(), 210 + 3 + 3 + 9);
    AesTiming v(o);
    for (int i = 0; i < 70; i++) v.memory_cycle(true, Place::Ram, 0x1004);
    v.internal_cycle(Place::Rom, 0x3000008);
    CHECK_EQ(v.ticks(), 210 + 3 + 9);
    o.time_links = M::Links::SeqTied;
    AesTiming w(o);
    for (int i = 0; i < 64; i++) w.internal_cycle(Place::Ram, 0x1008);
    CHECK_EQ(w.ticks(), 192 + 9);
}

TEST(time_refresh_keeps_beat, "time.refresh_period: a late refresh does not move the next request (INF, item 12: the counter runs on M8REF)") {
    AesOptions o;
    AesTiming t(o);
    for (int i = 0; i < 70; i++) t.memory_cycle(true, Place::Ram, 0x1004);
    t.memory_cycle(false, Place::Ram, 0x1008);
    CHECK_EQ(t.ticks(), 225);
    for (int i = 0; i < 53; i++) t.memory_cycle(true, Place::Ram, 0x1004);
    CHECK_EQ(t.ticks(), 384);
    t.memory_cycle(false, Place::Ram, 0x1008);
    CHECK_EQ(t.ticks(), 384 + 6 + 9);
}

TEST(time_links, "time.links: normal by default (INF); tied SEQ removes sequential cycles (INF)") {
    AesOptions d = no_refresh();
    AesTiming plain(d);
    plain.memory_cycle(true, Place::Ram, 0x1004);
    CHECK_EQ(plain.ticks(), 3);
    AesOptions o = no_refresh();
    o.time_links = M::Links::SeqTied;
    AesTiming t(o);
    t.memory_cycle(true, Place::Ram, 0x1004);
    CHECK_EQ(t.ticks(), 6);
}

TEST(time_ldr_keeps_core_switch, "time.reg_shift_next and time.ldr_reg_shift stay in ArmOptions: they change cycles, not costs (USER, 31 Oct 2026)") {
    AesOptions o;
    CHECK_EQ(o.cpu.time_reg_shift_next == ArmOptions::RegShiftNext::NFetch, true);
    CHECK_EQ(o.cpu.time_ldr_reg_shift == ArmOptions::LdrRegShiftTime::OneS, true);
}

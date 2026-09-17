// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

// Board timing for the 6502 Second Processor: DRAM refresh and write stretch
// (issue #70, docs/discussion/tube-coprocessor-board-timing.md).
//
// The 3 MHz cheese wedge is slower than 3 MHz for two board reasons: a DRAM
// refresh steals one cycle at the next opcode fetch every 176 crystal ticks
// (12 MHz / 16 / 11 = one per 44 nominal cycles), and every write cycle is
// stretched by one 12 MHz period (a write is 5 ticks where a read is 4). The
// internal 65C102 board has refresh (one in 64) and no write stretch. hoglet and
// tom_seddon measured 2.922 MHz (LDA zp) / 2.703 MHz (STA zp) on the wedge and
// 3.939 MHz for both on the 65C102 (stardot t=25167).
//
// The runner counts crystal ticks: BoardTiming gives ticks per host cycle and
// the tick cost of a read cycle, a write cycle, and a refresh hold. Over H host
// cycles the coprocessor is due T = H * ticks ticks; executing ticks E satisfy
// E + hold_ticks * (E / period) = T, and executed CPU cycles are E divided by
// the stream's mean ticks per cycle (LDA zp = 4, STA zp = 13/3). cycle_count()
// stays "CPU cycles executed": a refresh hold executes none.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>

using namespace beebium;

namespace {

// A 4 KB ROM (&F000-&FFFF): reset -> `entry`, then `entry` holds a tight loop of
// one 2-byte zero-page instruction repeated `reps` times, ending in JMP entry.
// LDA zp (&A5) is three read cycles; STA zp (&85) is two reads and one write.
std::array<uint8_t, 4096> make_zp_loop_rom(uint8_t opcode, uint16_t entry = 0xF800,
                                           unsigned reps = 256) {
    std::array<uint8_t, 4096> rom{};
    rom.fill(0xEA);  // NOP filler
    uint16_t off = entry - 0xF000;
    for (unsigned i = 0; i < reps; ++i) {
        rom[off++] = opcode;
        rom[off++] = 0x70;
    }
    rom[off++] = 0x4C;
    rom[off++] = static_cast<uint8_t>(entry & 0xFF);
    rom[off++] = static_cast<uint8_t>(entry >> 8);
    rom[0xFFC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0xFFD] = static_cast<uint8_t>(entry >> 8);
    return rom;
}

struct Driven { uint64_t executed; uint64_t holds; };

Driven drive(BoardTiming timing, uint8_t opcode, uint64_t host_cycles) {
    TubeUla tube;
    auto rom = make_zp_loop_rom(opcode);
    CoprocessorRunner runner(tube, rom, timing);
    runner.run_until(0);            // establish the time origin at host cycle 0
    runner.run_until(host_cycles);
    return {runner.cycle_count(), runner.refresh_hold_count()};
}

constexpr uint64_t kHostCyclesPerSecond = 2'000'000;  // host runs at 2 MHz
constexpr uint8_t LDA_ZP = 0xA5;  // 3 cycles, all reads
constexpr uint8_t STA_ZP = 0x85;  // 3 cycles, 2 reads + 1 write

// The measured boards.
constexpr BoardTiming kWedge{ClockRatio{6, 1}, 4, 5, 176, 1};
constexpr BoardTiming k65C102{ClockRatio{2, 1}, 1, 1, 64, 1};

// Executed CPU cycles over kHostCyclesPerSecond, from the tick model (see header).
double executed_model(const BoardTiming& t, double mean_ticks_per_cycle) {
    const double T = static_cast<double>(kHostCyclesPerSecond)
                   * t.ticks_per_host_cycle.numerator / t.ticks_per_host_cycle.denominator;
    const double period = t.refresh_period_ticks;
    const double hold = static_cast<double>(t.refresh_hold_cycles) * t.read_cycle_ticks;
    const double E = period == 0 ? T : T * period / (period + hold);
    return E / mean_ticks_per_cycle;
}

// Tolerance for the refresh figures: absorbs the SYNC-wait slack (a refresh
// waits for the next opcode fetch, so the true interval is period + up to one
// instruction), far tighter than the 2-8% gap to the nominal 3.00 / 4.00 MHz.
constexpr int64_t kTol = 20'000;

bool near(uint64_t got, double expected, int64_t tol) {
    return std::llabs(static_cast<int64_t>(got) - static_cast<int64_t>(expected)) <= tol;
}

}  // namespace

TEST_CASE("Board timing: wedge LDA-zp stream is ~2.93 MHz (refresh only)",
          "[coprocessor][board-timing][issue70]") {
    const double expected = executed_model(kWedge, 4.0);          // ~2,933,333
    const uint64_t got = drive(kWedge, LDA_ZP, kHostCyclesPerSecond).executed;
    INFO("wedge LDA zp executed " << got << " (expect ~" << expected << ", 2.93 MHz)");
    CHECK(near(got, expected, kTol));
    CHECK(got < kHostCyclesPerSecond * 3 / 2);  // slower than the nominal 3.00 MHz
}

TEST_CASE("Board timing: wedge STA-zp stream is ~2.70 MHz (refresh + write stretch)",
          "[coprocessor][board-timing][issue70]") {
    const double expected = executed_model(kWedge, 13.0 / 3.0);   // ~2,707,692
    const uint64_t got = drive(kWedge, STA_ZP, kHostCyclesPerSecond).executed;
    INFO("wedge STA zp executed " << got << " (expect ~" << expected << ", 2.70 MHz)");
    CHECK(near(got, expected, kTol));
}

TEST_CASE("Board timing: 65C102 LDA-zp stream is ~3.94 MHz (refresh only)",
          "[coprocessor][board-timing][issue70]") {
    const double expected = executed_model(k65C102, 1.0);         // ~3,938,461
    const uint64_t got = drive(k65C102, LDA_ZP, kHostCyclesPerSecond).executed;
    INFO("65C102 LDA zp executed " << got << " (expect ~" << expected << ", 3.94 MHz)");
    CHECK(near(got, expected, kTol));
    CHECK(got < kHostCyclesPerSecond * 2);  // slower than the nominal 4.00 MHz
}

TEST_CASE("Board timing: 65C102 STA-zp stream matches its LDA stream (no write stretch)",
          "[coprocessor][board-timing][issue70]") {
    const uint64_t lda = drive(k65C102, LDA_ZP, kHostCyclesPerSecond).executed;
    const uint64_t sta = drive(k65C102, STA_ZP, kHostCyclesPerSecond).executed;
    INFO("65C102 LDA " << lda << " STA " << sta);
    // This board does not stretch writes, so the two streams run at the same rate.
    CHECK(std::llabs(static_cast<int64_t>(lda) - static_cast<int64_t>(sta)) <= 4);
}

TEST_CASE("Board timing: with refresh off, reads cost 4 ticks and writes 5 (the stretch)",
          "[coprocessor][board-timing][issue70]") {
    // No refresh isolates the write stretch. All-read stream: every cycle 4
    // ticks, so executed is exactly the due ticks / 4.
    const BoardTiming no_refresh{ClockRatio{6, 1}, 4, 5, 0, 1};
    const uint64_t lda = drive(no_refresh, LDA_ZP, kHostCyclesPerSecond).executed;
    CHECK(lda == kHostCyclesPerSecond * 6 / 4);  // 12,000,000 ticks / 4 = 3,000,000 exactly

    // Store stream: the one write in three costs 5 not 4, so it runs at 4 / (mean
    // ticks per cycle) of the read rate. tom_seddon measured this ratio at 0.925;
    // the loop (256 STA zp + JMP) gives 4 * 771 / 3340 = 0.9233.
    const uint64_t sta = drive(no_refresh, STA_ZP, kHostCyclesPerSecond).executed;
    const double ratio = static_cast<double>(sta) / static_cast<double>(lda);
    INFO("STA/LDA rate ratio " << ratio << " (expect ~0.923 from the write stretch)");
    CHECK(ratio < 1.0);
    CHECK(std::abs(ratio - 0.9233) < 0.002);
}

TEST_CASE("Board timing: the coprocessor never runs ahead of due time by more than a cycle",
          "[coprocessor][board-timing][issue70][skew]") {
    // Skew in ticks: after every run_until the unspent tick budget is bounded by
    // one cycle's cost (the loop stops when it cannot afford a read, and a write
    // overshoots by at most one tick), so the coprocessor stays within one cycle
    // -- under one host cycle of 6 ticks -- of due time. Drive an STA stream (the
    // widest cycle) over a long, irregular sequence of host times.
    TubeUla tube;
    auto rom = make_zp_loop_rom(STA_ZP);
    CoprocessorRunner runner(tube, rom, kWedge);
    runner.run_until(0);
    uint64_t h = 0;
    for (int i = 0; i < 5000; ++i) {
        h += 1 + (i % 7);  // irregular host-time steps
        runner.run_until(h);
        INFO("after run_until(" << h << ") tick budget " << runner.tick_budget());
        CHECK(runner.tick_budget() < static_cast<int64_t>(kWedge.read_cycle_ticks));
        CHECK(runner.tick_budget() > -static_cast<int64_t>(kWedge.write_cycle_ticks));
    }
}

TEST_CASE("Board timing: refresh holds account exactly for the stolen cycles",
          "[coprocessor][board-timing][issue70]") {
    // Wedge, all-read stream: every executed read cycle and every refresh hold
    // costs exactly 4 ticks, and 12,000,000 ticks is a whole multiple of 4, so
    // executed cycles plus refresh holds account for every due tick exactly.
    const Driven d = drive(kWedge, LDA_ZP, kHostCyclesPerSecond);
    INFO("executed " << d.executed << " + holds " << d.holds << " = " << d.executed + d.holds);
    CHECK(d.holds > 0);
    CHECK(d.executed + d.holds == kHostCyclesPerSecond * 6 / 4);  // 3,000,000
    // About one hold per 44 executed read cycles (176 ticks / 4).
    CHECK(near(d.holds, static_cast<double>(d.executed) / 44.0, 2000));
}

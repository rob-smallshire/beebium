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

// Reproduces issue #70: the 6502 Second Processor runs ~4.2% faster than
// hardware because two board-level timing effects are not modelled.
//
// EXPECTED TO FAIL until #70 is fixed (test-first red); no fix is included here.
//
// acheton1984 measured ten "Tak on 6502" benchmarks 4.23% fast on average on a
// real 6502 Second Processor vs Beebium (#70). tube-architect's design note
// docs/discussion/tube-coprocessor-board-timing.md attributes the whole gap to
// two effects on the 3 MHz "cheese wedge" board, neither in the CPU:
//   1. DRAM refresh steals one cycle at the next opcode fetch every 176 crystal
//      periods (12 MHz / 16 / 11 = one refresh per 44 nominal 3 MHz cycles).
//   2. Every write cycle is stretched by one 12 MHz period, so a write cycle is
//      5 crystal periods where a read cycle is 4 (service manual, RAM timing).
// The internal 65C102 co-processor board has refresh (one in 64) and no write
// stretch. tom_seddon measured the wedge at 2.922 MHz for an LDA-zp loop but
// 2.703 MHz for an STA-zp loop, and the 65C102 at 3.939 MHz for both
// (https://stardot.org.uk/forums/viewtopic.php?t=25167).
//
// The model counts CRYSTAL TICKS, not CPU cycles: read cycle = 4 ticks, write
// cycle = 5, refresh period = 176 ticks with a one-cycle (4-tick) hold, and the
// host runs at 2 MHz so the wedge is 6 ticks per host cycle (12 MHz) and the
// 65C102 is 2 (4 MHz). Over H host cycles the coprocessor is due T = H * ticks
// ticks; of those, the executing ticks E satisfy E + hold * (E / period) = T
// (the refresh timer counts executing ticks and stalls during the hold), and the
// executed CPU cycles are E / (mean ticks per cycle for the instruction stream).
//
// This test drives a CoprocessorRunner over 2,000,000 host cycles (one emulated
// second) on each board with two instruction streams -- all LDA zp (every cycle
// a read) and all STA zp (one write cycle in three) -- and asserts the executed
// CPU-cycle count matches the hardware figure. Today the runner charges nothing
// per cycle: run_until() executes exactly cycles_due (host * ratio) CPU cycles
// and cycle_count() counts them, so every stream reports the nominal 3,000,000 /
// 4,000,000 regardless of reads vs writes -- that is the red.
//
// NOTE ON THE API: the runner is constructed today with ClockRatio (3/2 wedge,
// 2/1 65C102). The design note replaces that with a per-board BoardTiming and
// makes the clock count ticks; when the fix lands, the construction below and
// the ratio arithmetic move to BoardTiming, and cycle_count() then reports the
// executed (post-refresh, post-stretch) figures these assertions expect.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>

using namespace beebium;

namespace {

// A 4 KB ROM (&F000-&FFFF): reset -> `entry`, then `entry` holds a tight loop of
// one 2-byte zero-page instruction (opcode, &70) repeated `reps` times, ending
// in JMP entry. LDA zp (&A5) is three read cycles; STA zp (&85) is two reads and
// one write. The JMP back is 0.1% of the stream and does not shift the figures.
std::array<uint8_t, 4096> make_zp_loop_rom(uint8_t opcode, uint16_t entry = 0xF800,
                                           unsigned reps = 256) {
    std::array<uint8_t, 4096> rom{};
    rom.fill(0xEA);  // NOP filler
    uint16_t off = entry - 0xF000;
    for (unsigned i = 0; i < reps; ++i) {
        rom[off++] = opcode;   // LDA/STA zp
        rom[off++] = 0x70;     // zero-page address (its contents are irrelevant)
    }
    rom[off++] = 0x4C;                                    // JMP abs
    rom[off++] = static_cast<uint8_t>(entry & 0xFF);
    rom[off++] = static_cast<uint8_t>(entry >> 8);
    rom[0xFFC] = static_cast<uint8_t>(entry & 0xFF);       // reset vector -> entry
    rom[0xFFD] = static_cast<uint8_t>(entry >> 8);
    return rom;
}

uint64_t executed_over(unsigned ratio_num, unsigned ratio_den, uint8_t opcode,
                       uint64_t host_cycles) {
    TubeUla tube;
    auto rom = make_zp_loop_rom(opcode);
    CoprocessorRunner runner(tube, rom, ClockRatio{ratio_num, ratio_den});
    runner.run_until(0);            // establish the time origin at host cycle 0
    runner.run_until(host_cycles);
    return runner.cycle_count();
}

constexpr uint64_t kHostCyclesPerSecond = 2'000'000;  // host runs at 2 MHz
constexpr uint8_t LDA_ZP = 0xA5;  // 3 cycles, all reads
constexpr uint8_t STA_ZP = 0x85;  // 3 cycles, 2 reads + 1 write

// Executed CPU cycles over kHostCyclesPerSecond, from the tick model.
//   T          total ticks = host_cycles * ticks_per_host
//   E          executing ticks: E + hold*(E/period) = T  =>  E = T*period/(period+hold)
//   executed   E / mean_ticks_per_cycle
// mean_ticks_per_cycle: LDA zp = 4 (all reads); STA zp = (4+4+5)/3 = 13/3.
double executed_model(double ticks_per_host, double period_ticks, double hold_ticks,
                      double mean_ticks_per_cycle) {
    const double T = static_cast<double>(kHostCyclesPerSecond) * ticks_per_host;
    const double E = T * period_ticks / (period_ticks + hold_ticks);
    return E / mean_ticks_per_cycle;
}

// Tolerance: wide enough to absorb the model's SYNC-wait slack (the refresh
// interval is "176 ticks plus the wait for SYNC", so the true figure is a little
// under this), but far tighter than the 2-8% gap to today's nominal count.
constexpr int64_t kTol = 20'000;

}  // namespace

TEST_CASE("Issue #70: wedge 6502 Second Processor, LDA-zp stream ~2.93 MHz (refresh only)",
          "[coprocessor][board-timing][issue70]") {
    // Wedge: 6 ticks/host, read=4, write=5, refresh period 176, hold 1 cycle=4.
    const uint64_t expected = static_cast<uint64_t>(
        executed_model(6.0, 176.0, 4.0, 4.0));      // ~2,933,333 (2.93 MHz)
    const uint64_t nominal = kHostCyclesPerSecond * 3 / 2;  // 3,000,000 today
    const uint64_t got = executed_over(3, 2, LDA_ZP, kHostCyclesPerSecond);

    INFO("wedge LDA zp: expected ~" << expected << " (2.93 MHz), nominal " << nominal
         << ", got " << got << (got == nominal ? " (3.00 MHz, no board timing modelled)" : ""));
    CHECK(std::llabs(static_cast<int64_t>(got) - static_cast<int64_t>(expected)) <= kTol);
}

TEST_CASE("Issue #70: wedge 6502 Second Processor, STA-zp stream ~2.70 MHz (refresh + write stretch)",
          "[coprocessor][board-timing][issue70]") {
    // STA zp adds one stretched write cycle in three: mean 13/3 ticks per cycle.
    const uint64_t expected = static_cast<uint64_t>(
        executed_model(6.0, 176.0, 4.0, 13.0 / 3.0));  // ~2,707,692 (2.70 MHz)
    const uint64_t nominal = kHostCyclesPerSecond * 3 / 2;  // 3,000,000 today
    const uint64_t got = executed_over(3, 2, STA_ZP, kHostCyclesPerSecond);

    INFO("wedge STA zp: expected ~" << expected << " (2.70 MHz), nominal " << nominal
         << ", got " << got << (got == nominal ? " (3.00 MHz, no board timing modelled)" : ""));
    CHECK(std::llabs(static_cast<int64_t>(got) - static_cast<int64_t>(expected)) <= kTol);
}

TEST_CASE("Issue #70: 65C102 co-processor, LDA-zp stream ~3.94 MHz (refresh only, no write stretch)",
          "[coprocessor][board-timing][issue70]") {
    // 65C102: expressed in its own 4 MHz cycles -- 2 ticks/host, read=write=1,
    // refresh period 64, hold 1. No write stretch, so LDA and STA match.
    const uint64_t expected = static_cast<uint64_t>(
        executed_model(2.0, 64.0, 1.0, 1.0));       // ~3,938,461 (3.94 MHz)
    const uint64_t nominal = kHostCyclesPerSecond * 2 / 1;  // 4,000,000 today
    const uint64_t got = executed_over(2, 1, LDA_ZP, kHostCyclesPerSecond);

    INFO("65C102 LDA zp: expected ~" << expected << " (3.94 MHz), nominal " << nominal
         << ", got " << got << (got == nominal ? " (4.00 MHz, no board timing modelled)" : ""));
    CHECK(std::llabs(static_cast<int64_t>(got) - static_cast<int64_t>(expected)) <= kTol);
}

TEST_CASE("Issue #70: 65C102 co-processor, STA-zp stream ~3.94 MHz (no write stretch on this board)",
          "[coprocessor][board-timing][issue70]") {
    // Same figure as the LDA stream: this board does not stretch writes.
    const uint64_t expected = static_cast<uint64_t>(
        executed_model(2.0, 64.0, 1.0, 1.0));       // ~3,938,461 (3.94 MHz)
    const uint64_t nominal = kHostCyclesPerSecond * 2 / 1;  // 4,000,000 today
    const uint64_t got = executed_over(2, 1, STA_ZP, kHostCyclesPerSecond);

    INFO("65C102 STA zp: expected ~" << expected << " (3.94 MHz), nominal " << nominal
         << ", got " << got << (got == nominal ? " (4.00 MHz, no board timing modelled)" : ""));
    CHECK(std::llabs(static_cast<int64_t>(got) - static_cast<int64_t>(expected)) <= kTol);
}

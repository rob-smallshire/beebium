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

// Tests for the parasite execution runner.
//
// ParasiteRunner owns the parasite emulation engine: CPU, memory map,
// Tube port, and execution loop. It is the parasite's analogue of
// Machine<Hardware> on the host side.
//
// These tests verify:
//   - Construction and ROM loading
//   - Execution via run() and step_instruction()
//   - Pause/resume for debugger integration
//   - Clean shutdown coordination

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <thread>
#include <vector>

using namespace beebium;

// Helper: create a 2 KB ROM with a known reset vector and NOP fill.
static std::array<uint8_t, 2048> make_nop_rom(uint16_t entry = 0xF800) {
    std::array<uint8_t, 2048> rom{};
    rom.fill(0xEA);  // NOP
    // Reset vector at ROM offset 0x7FC-0x7FD (maps to &FFFC-&FFFD)
    rom[0x7FC] = static_cast<uint8_t>(entry & 0xFF);
    rom[0x7FD] = static_cast<uint8_t>(entry >> 8);
    // IRQ vector -> &F900 (ROM offset 0x100), with RTI
    rom[0x7FE] = 0x00;
    rom[0x7FF] = 0xF9;
    rom[0x100] = 0x40;  // RTI
    // NMI vector -> &F980 (ROM offset 0x180), with RTI
    rom[0x7FA] = 0x80;
    rom[0x7FB] = 0xF9;
    rom[0x180] = 0x40;  // RTI
    return rom;
}

// ===========================================================================
// Construction and initial state
// ===========================================================================

TEST_CASE("ParasiteRunner construction with ROM", "[parasite][runner]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);

    CHECK(runner.cycle_count() == 0);
    CHECK(runner.memory_map().boot_mode());
}

TEST_CASE("ParasiteRunner reset initialises CPU at reset vector", "[parasite][runner]") {
    TubeUla tube;
    auto rom = make_nop_rom(0xF850);

    ParasiteRunner runner(tube, rom);
    runner.reset();

    // Execute reset sequence (7 cycles)
    uint64_t cycles = runner.step_instruction();
    CHECK(cycles == 7);
    CHECK(runner.cpu().abus.w == 0xF850);
}

// ===========================================================================
// Execution
// ===========================================================================

TEST_CASE("ParasiteRunner run executes cycles", "[parasite][runner][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);
    runner.reset();

    runner.run(100);
    CHECK(runner.cycle_count() == 100);
}

TEST_CASE("ParasiteRunner step_instruction returns cycle count", "[parasite][runner][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);
    runner.reset();

    uint64_t reset_cycles = runner.step_instruction();
    CHECK(reset_cycles == 7);

    uint64_t nop_cycles = runner.step_instruction();
    CHECK(nop_cycles == 2);
}

// ===========================================================================
// Pause/resume (debugger support)
// ===========================================================================

TEST_CASE("ParasiteRunner pause stops execution", "[parasite][runner][debug]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);
    runner.reset();

    runner.pause();
    CHECK(runner.is_paused());

    // run() returns immediately when paused (single-threaded: no blocking)
    auto cycle_before = runner.cycle_count();
    runner.run(1000);
    CHECK(runner.cycle_count() == cycle_before);

    // Resume and run
    runner.resume();
    CHECK_FALSE(runner.is_paused());
    runner.run(1000);
    CHECK(runner.cycle_count() > cycle_before);
}

// ===========================================================================
// Component access
// ===========================================================================

TEST_CASE("ParasiteRunner provides access to components", "[parasite][runner]") {
    TubeUla tube;
    auto rom = make_nop_rom();

    ParasiteRunner runner(tube, rom);

    // CPU access
    CHECK(runner.cpu().config == &M6502_rockwell65c02_config);

    // Memory map access
    CHECK(runner.memory_map().boot_mode());

    // Tube port access
    CHECK_FALSE(runner.tube_port().pirq());

    // Const access
    const ParasiteRunner& crunner = runner;
    CHECK(crunner.cycle_count() == 0);
    CHECK(crunner.cpu().config == &M6502_rockwell65c02_config);
}

// ===========================================================================
// Coprocessor contract: host-time driving via run_until()
// ===========================================================================

TEST_CASE("ParasiteRunner run_until matches the old 3:2 accumulator per-call sequence",
          "[parasite][runner][coprocessor]") {
    // Equivalence oracle. The removed TubeSocket accumulator advanced a phase
    // by the numerator on each per-host-cycle call and ran a parasite tick each
    // time the phase reached the denominator. Encode that algorithm inline and
    // require run_until(t) for t = 1..N to run exactly the same cycles per call.
    //
    // The clock's time base is undefined until the first run_until, which
    // defines the origin and runs nothing. Establish the origin at host time 0
    // so that run_until(1) is the first host cycle, matching the accumulator's
    // first call. Cycles run per call are counted from the CPU cycle counter --
    // each step() is exactly one tick.
    TubeUla tube;
    auto rom = make_nop_rom();
    ParasiteRunner runner(tube, rom, ClockRatio{3, 2});
    runner.run_until(0);   // establish the origin at host time 0

    const uint64_t N = 32;
    const uint32_t num = 3, den = 2;
    uint32_t phase = 0;
    std::vector<uint64_t> expected, actual;

    uint64_t prev = runner.cycle_count();
    for (uint64_t t = 1; t <= N; ++t) {
        // Oracle: the old accumulator's cycles for this host cycle.
        phase += num;
        uint64_t exp = 0;
        while (phase >= den) { phase -= den; ++exp; }
        expected.push_back(exp);

        // Actual: cycles the runner ran for this host cycle.
        runner.run_until(t);
        const uint64_t now = runner.cycle_count();
        actual.push_back(now - prev);
        prev = now;
    }

    CHECK(actual == expected);
    // Sanity: the sequence opens 1, 2, 1, 2, ...
    CHECK(actual[0] == 1);
    CHECK(actual[1] == 2);
    CHECK(actual[2] == 1);
    CHECK(actual[3] == 2);
    // Total equals floor(N * 3 / 2) with no drift.
    uint64_t total = 0;
    for (auto c : actual) total += c;
    CHECK(total == N * num / den);
}

TEST_CASE("ParasiteRunner run_until while paused advances time but runs no cycles, no catch-up",
          "[parasite][runner][coprocessor][debug]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    ParasiteRunner runner(tube, rom, ClockRatio{3, 2});
    runner.reset();

    runner.run_until(0);   // establish origin at host time 0
    const uint64_t base = runner.cycle_count();

    runner.pause();
    REQUIRE(runner.is_paused());

    // A long paused interval: time advances to 100, but nothing runs.
    runner.run_until(100);
    CHECK(runner.cycle_count() == base);

    // After resuming, the next call runs ONLY the cycles due for the new
    // interval [100, 101] -- one host cycle -- and does NOT catch up the ~150
    // cycles that fell in the paused interval.
    runner.resume();
    REQUIRE_FALSE(runner.is_paused());
    runner.run_until(101);
    CHECK(runner.cycle_count() - base == 1);
}

TEST_CASE("ParasiteRunner reset rebases the clock: a smaller host time is accepted",
          "[parasite][runner][coprocessor]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    ParasiteRunner runner(tube, rom, ClockRatio{3, 2});
    runner.reset();

    // Run well into a session.
    runner.run_until(1000);
    runner.run_until(1010);

    // A hard host reset zeroes the host cycle count and propagates across the
    // Tube cable, so host time goes backwards. reset() must accept that.
    runner.reset();
    const uint64_t base = runner.cycle_count();

    // A run_until with a much smaller host time is accepted, defines the new
    // origin, and runs nothing.
    runner.run_until(5);
    CHECK(runner.cycle_count() == base);

    // Subsequent calls run from the new origin: [5, 7] is two host cycles,
    // floor(2 * 3 / 2) = 3 parasite cycles.
    runner.run_until(7);
    CHECK(runner.cycle_count() - base == 3);
}

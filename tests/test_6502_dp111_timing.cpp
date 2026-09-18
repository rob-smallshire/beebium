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

// Bare-core harness for Dominic Plunkett's (dp111) 6502 instruction timing suite,
// vector builds. See tests/assets/6502timing/README.md for provenance.
//
// The vector builds (6502timing.6502, 65C02timing.6502) are ORG &2000 raw images
// with a fixed vector table the harness patches or services:
//
//   &2000  entry           JMP starttest
//   &2010  print character  JMP &FFE3     (A = char, preserves X, Y)
//   &2020  initialise       programs the 6522 VIA (timer 1, one-shot)
//   &2030  start timer      LSR A : STA &FE64 : STA &FE65 : RTS
//   &2040  read timer       LDA &FE64 : ASL A : TAX : RTS
//   &2050  end of tests     STA &FCD0 : RTS   (A = number of failures)
//
// Timer model. The suite is written for a BBC whose 6522 VIA is clocked at 1 MHz
// while the CPU runs at 2 MHz, so the start vector halves the requested value
// (LSR A) before loading timer 1 and the stop vector doubles the value read back
// (ASL A). The TIME macro loads (time + 8) * 2, where 8 (timeoffset) calibrates
// out the fixed overhead of the start/stop vectors plus the two JSRs; a "Warning
// Timer Zero Error" probe (TIME 0) confirms that calibration at startup. The
// instruction under test runs twice inside the window, and the check routine
// passes only when the doubled counter value read back is zero -- that is, when
// exactly (time + 8) VIA ticks elapsed.
//
// The core has no VIA and no 2 MHz/1 MHz clock domain: one call to the transfer
// function is one CPU cycle. So the harness keeps the real start/stop vector code
// (its cycles are part of the calibration) and models only timer 1 behind the
// &FE64/&FE65 bus accesses: the high-byte write starts a down-count, and a low-byte
// read returns (load - elapsed_cpu_cycles / 2) & 0xFF. One VIA tick is two CPU
// cycles; the /2 is exact at the sample point when the instruction's cycle count is
// right, which is the whole point of the test. kTimerPhase is the single fixed
// offset (in CPU cycles) between the high-byte write and the counter starting to
// run; it is pinned by the zero-error probe and cannot mask a per-instruction error,
// since no global offset makes a mistimed instruction land on zero while the rest
// still do.

#include <catch2/catch_test_macros.hpp>
#include <6502/6502.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

// I/O addresses the harness services rather than treating as plain memory.
constexpr uint16_t kViaT1CL = 0xFE64;   // timer 1 counter low  (start writes, stop reads)
constexpr uint16_t kViaT1CH = 0xFE65;   // timer 1 counter high (write starts the count)
constexpr uint16_t kPassFailReg = 0xFCD0;  // end-of-test write: value = failure count
constexpr uint16_t kCharPort = 0xFCD1;  // harness console: STA here appends one byte
constexpr uint16_t kOsAsciVector = 0xFFE3;  // &2010 jumps here for character output
constexpr uint16_t kLoadAddress = 0x2000;

// Offset in CPU cycles between the high-byte timer write and the counter starting
// to decrement. Pinned by the suite's "Timer Zero Error" probe; see the file header.
constexpr int kTimerPhase = 0;

// One VIA tick is two CPU cycles, so perturbing the phase by two cycles moves
// every measured time by exactly one tick. Used by the negative-control test to
// prove the harness can see a mistimed instruction, not just report zero.
constexpr int kOneTickPerturbation = 2;

struct RunResult {
    bool completed = false;   // the suite wrote its failure count to &FCD0
    int failures = -1;        // that count, or -1 if the run never reached the end
    std::string console;      // everything the suite printed, via the &2010 vector
    uint64_t cycles = 0;      // CPU cycles executed
};

// Load a dp111 vector build (a raw ORG &2000 image) from tests/assets/6502timing/.
std::vector<uint8_t> load_vector_build(const std::string& filename) {
    std::string filepath = std::string(BEEBIUM_TEST_ASSETS_DIR) + "/6502timing/" + filename;
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return {};
    }
    file.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    return data;
}

RunResult run_vector_build(const std::vector<uint8_t>& image,
                           const M6502Config* config,
                           int phase = kTimerPhase,
                           uint64_t max_cycles = 60'000'000) {
    RunResult result;

    auto mem = std::make_unique<std::array<uint8_t, 65536>>();
    mem->fill(0);

    if (kLoadAddress + image.size() > mem->size()) {
        return result;
    }
    std::memcpy(mem->data() + kLoadAddress, image.data(), image.size());

    // Character output stub at the &2010 vector's target: STA &FCD1 : RTS. This
    // preserves A, X and Y (as osasci must) and lets the harness capture every
    // printed byte through the &FCD1 write path.
    (*mem)[kOsAsciVector + 0] = 0x8D;  // STA abs
    (*mem)[kOsAsciVector + 1] = kCharPort & 0xFF;
    (*mem)[kOsAsciVector + 2] = kCharPort >> 8;
    (*mem)[kOsAsciVector + 3] = 0x60;  // RTS

    // Reset vector -> entry point.
    (*mem)[0xFFFC] = kLoadAddress & 0xFF;
    (*mem)[0xFFFD] = kLoadAddress >> 8;

    M6502 cpu;
    M6502_Init(&cpu, config);
    M6502_Reset(&cpu);

    bool timer_running = false;
    uint64_t timer_start_cycle = 0;
    int timer_load = 0;

    uint64_t cycles = 0;
    while (cycles < max_cycles) {
        (*cpu.tfn)(&cpu);
        ++cycles;

        if (cpu.read) {
            if (timer_running && cpu.abus.w == kViaT1CL) {
                uint64_t elapsed = cycles - timer_start_cycle;
                long ticks = static_cast<long>((elapsed + phase) / 2);
                cpu.dbus = static_cast<uint8_t>((timer_load - ticks) & 0xFF);
            } else {
                cpu.dbus = (*mem)[cpu.abus.w];
            }
        } else {
            const uint16_t addr = cpu.abus.w;
            const uint8_t value = cpu.dbus;
            if (addr == kViaT1CH) {
                timer_running = true;
                timer_start_cycle = cycles;
                timer_load = value;
            } else if (addr == kCharPort) {
                result.console.push_back(static_cast<char>(value));
            } else if (addr == kPassFailReg) {
                result.failures = value;
                result.completed = true;
                break;
            } else {
                (*mem)[addr] = value;
            }
        }
    }

    result.cycles = cycles;
    return result;
}

// Pull out the failing lines the suite printed. Every error line is the instruction
// name padded to a fixed column followed by a two-hex-digit cycle error; the summary
// lines ("Checking documented instructions...", "Done!", "Number of failures ...")
// are informational. Returned verbatim for a diagnostic on assertion failure.
std::string diagnostics(const RunResult& result) {
    std::string out;
    out += "cycles=" + std::to_string(result.cycles);
    out += " completed=" + std::string(result.completed ? "yes" : "no");
    out += " failures=" + std::to_string(result.failures) + "\n";
    out += "--- captured output ---\n";
    out += result.console;
    return out;
}

}  // namespace

TEST_CASE("dp111 timing, NMOS core, vector build", "[6502][dp111][timing]") {
    auto image = load_vector_build("6502timing.6502");
    REQUIRE(!image.empty());

    RunResult result = run_vector_build(image, &M6502_nmos6502_config);
    INFO(diagnostics(result));
    REQUIRE(result.completed);
    REQUIRE(result.failures == 0);
}

TEST_CASE("dp111 timing, Rockwell 65C02 core, vector build", "[6502][dp111][timing]") {
    // M6502_rockwell65c02_config is the configuration the acorn-65c02-coprocessor
    // and acorn-65c102-coprocessor plugins run on, so this directly checks the
    // instruction cycle counts the #70 board-timing work assumed to be correct.
    auto image = load_vector_build("65C02timing.6502");
    REQUIRE(!image.empty());

    RunResult result = run_vector_build(image, &M6502_rockwell65c02_config);
    INFO(diagnostics(result));
    REQUIRE(result.completed);
    REQUIRE(result.failures == 0);
}

// Negative control. The two guards above assert zero failures, so on their own
// they cannot distinguish a correct core from a harness that always reports zero.
// Perturb the timer by exactly one VIA tick (two CPU cycles) and the suite must
// then see every instruction, and its own "Timer Zero Error" probe, as mistimed:
// the run still completes (the instruction stream is unaffected, only the timing
// comparison), but with a non-zero failure count and the zero-error warning in the
// output. That is proof the guards can actually catch a mistimed instruction.
TEST_CASE("dp111 timing harness detects a one-tick error (negative control)",
          "[6502][dp111][timing]") {
    auto nmos = load_vector_build("6502timing.6502");
    REQUIRE(!nmos.empty());
    RunResult nmos_result =
        run_vector_build(nmos, &M6502_nmos6502_config, kOneTickPerturbation);
    INFO(diagnostics(nmos_result));
    REQUIRE(nmos_result.completed);
    REQUIRE(nmos_result.failures > 0);
    REQUIRE(nmos_result.console.find("Warning Timer Zero Error") != std::string::npos);

    auto rockwell = load_vector_build("65C02timing.6502");
    REQUIRE(!rockwell.empty());
    RunResult rockwell_result =
        run_vector_build(rockwell, &M6502_rockwell65c02_config, kOneTickPerturbation);
    INFO(diagnostics(rockwell_result));
    REQUIRE(rockwell_result.completed);
    REQUIRE(rockwell_result.failures > 0);
    REQUIRE(rockwell_result.console.find("Warning Timer Zero Error") !=
            std::string::npos);
}

// The generic M6502_cmos6502_config is deliberately not run against 65C02timing.6502.
// That build exercises the Rockwell bit instructions RMB/SMB/BBR/BBS, which the
// generic CMOS configuration does not implement: it decodes their opcode slots
// (0x07/0x0F/0x17/0x1F/... ) as CMOS NOPs of a different length, so BBR/BBS -- which
// the build assembles as three-byte zero-page/relative branches -- have their two
// operand bytes executed as opcodes. The instruction stream desyncs and the suite
// never writes its failure count. This is a configuration-scope mismatch (only the
// Rockwell config carries those instructions, and it is the one the coprocessors
// use), not a timing defect in the core, so there is nothing here to guard.

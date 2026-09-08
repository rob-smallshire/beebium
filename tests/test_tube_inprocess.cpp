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

// In-process Tube tests: CoprocessorRunner backed by TubeUla.
//
// These tests validate the in-process architecture where the host and
// coprocessor share a TubeUla in the same process. The host accesses TubeUla
// via host_read/host_write (as TubeSocket would), and the coprocessor accesses
// it via coprocessor_read/coprocessor_write (through CoprocessorRunner).
//
// The coprocessor runs the real Acorn Tube 6502 Client ROM, proving the in-process
// data path works with real CPU execution.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;

static constexpr const char* ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr size_t ROM_SIZE = 2048;

static std::array<uint8_t, ROM_SIZE> load_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());

    std::array<uint8_t, ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), ROM_SIZE);
    REQUIRE(file.gcount() == ROM_SIZE);
    return rom;
}

static constexpr uint64_t BOOT_CYCLES = 100000;

// ===========================================================================
// Single-threaded: host reads banner via TubeUla host interface
// ===========================================================================

TEST_CASE("In-process: host reads banner from R1 FIFO", "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    CoprocessorRunner runner(tube, rom);
    runner.reset();

    // Run coprocessor -- it writes the boot banner to R1 P-to-H FIFO.
    runner.run(BOOT_CYCLES);

    // Host reads R1 status via TubeUla host interface.
    uint8_t status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    // Read the expected 24-byte banner.
    static constexpr uint8_t expected_banner[] = {
        0x0A,
        'A', 'c', 'o', 'r', 'n', ' ',
        'T', 'U', 'B', 'E', ' ',
        '6', '5', '0', '2', ' ',
        '6', '4', 'K',
        0x0A, 0x0A, 0x0D, 0x00
    };
    static_assert(sizeof(expected_banner) == 24);

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(tube.host_read(1) == expected_banner[i]);
    }

    // FIFO empty -- DATA_AVAILABLE clears.
    status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) == 0);
}

// ===========================================================================
// Sequential: host reads banner after coprocessor boot
// ===========================================================================

TEST_CASE("In-process: host reads banner after coprocessor boot", "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    CoprocessorRunner runner(tube, rom);
    runner.reset();

    // Run coprocessor -- it writes the boot banner to R1 P-to-H FIFO.
    runner.run(BOOT_CYCLES);

    // Host reads banner via TubeUla.
    uint8_t status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    static constexpr uint8_t expected_banner[] = {
        0x0A,
        'A', 'c', 'o', 'r', 'n', ' ',
        'T', 'U', 'B', 'E', ' ',
        '6', '5', '0', '2', ' ',
        '6', '4', 'K',
        0x0A, 0x0A, 0x0D, 0x00
    };

    for (int i = 0; i < 24; ++i) {
        INFO("FIFO position: " << i);
        CHECK(tube.host_read(1) == expected_banner[i]);
    }
}

// ===========================================================================
// Sequential: R2 handshake with coprocessor
// ===========================================================================

TEST_CASE("In-process: R2 handshake with coprocessor", "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    CoprocessorRunner runner(tube, rom);
    runner.reset();

    // Boot phase
    runner.run(BOOT_CYCLES);

    // Verify banner is available
    uint8_t status = tube.host_read(0);
    REQUIRE((status & TubeUla::DATA_AVAILABLE) != 0);

    // Drain the banner
    for (int i = 0; i < 24; ++i) {
        tube.host_read(1);
    }

    // Write a dummy byte to R2 (OSRDCH command)
    tube.host_write(3, 0x00);

    // Run coprocessor more to process the R2 data
    runner.run(500000);

    // If we got here without hang, the R2 handshake worked.
    CHECK(true);
}

// ===========================================================================
// Breakpoints fire on the live tick() path (single-threaded ticking from
// Machine::step()), not only in run().
// ===========================================================================

TEST_CASE("In-process: coprocessor breakpoint fires on the tick() path",
          "[tube][inprocess]") {
    auto rom = load_rom();
    TubeUla tube;
    CoprocessorRunner runner(tube, rom);
    runner.reset();

    // Boot into real execution, then align to a clean instruction boundary so
    // the next opcode to execute is well-defined.
    runner.run(BOOT_CYCLES);
    runner.step_instruction();
    const uint16_t target_pc = runner.pc();

    // A breakpoint at the about-to-execute PC, with a hit callback that pauses --
    // exactly what the debugger service does to stop on a breakpoint.
    bool fired = false;
    uint16_t fired_pc = 0;
    runner.set_breakpoint_entries(
        {BreakpointEntry{/*id=*/1, /*start=*/target_pc,
                         /*end=*/static_cast<uint32_t>(target_pc) + 1}});
    runner.set_breakpoint_hit_callback(
        [&](const BreakpointEntry& /*bp*/, uint16_t pc) {
            fired = true;
            fired_pc = pc;
            runner.pause();
        });

    // tick() is one cycle of the live path: TubeSocket::run_coprocessor_until()
    // -> Coprocessor::run_until() -> step() per due cycle. It must check the
    // breakpoint BEFORE executing the instruction at target_pc.
    // (Before the fix the check lived only in run(), so this never fired.)
    runner.tick();

    CHECK(fired);
    CHECK(fired_pc == target_pc);
    CHECK(runner.is_paused());
    CHECK(runner.pc() == target_pc);  // paused before executing the breakpoint instruction
}

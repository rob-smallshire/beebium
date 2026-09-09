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

// test_boot_tube.cpp
//
// Integration test for BBC Model B boot with a 65C02 second processor.
//
// This is the full-stack integration test: a host Model B and a coprocessor
// 65C02 share a TubeUla, both run their real boot ROMs, and the test
// verifies that the coprocessor's banner ("Acorn TUBE 6502 64K") appears
// on the host's MODE 7 screen.
//
// The boot sequence:
//   1. Host resets, MOS starts initialisation
//   2. Coprocessor boots from its 2 KB client ROM, fills R1 P-to-H FIFO
//      with the banner string (24 bytes)
//   3. Host MOS writes &81 to &FEE0 (set Q), reads back, detects Tube ULA
//   4. Host MOS issues service call &FF -- DNFS ROM handles this and
//      sets the Tube presence flag at &027A
//   5. Host MOS reads the banner from R1 and prints it on screen
//   6. Host MOS attempts language transfer via R2/R4 (coprocessor responds)
//
// The DNFS ROM is required because MOS issues service call &FF after
// detecting Tube hardware. Without a ROM that claims this call, the
// Tube presence flag is never set, and MOS ignores the Tube.
//
// The coprocessor is installed on the TubeSocket and ticked automatically
// by Machine::step() at a 3:2 clock ratio (3 MHz coprocessor / 2 MHz host).

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_econet_helpers.hpp"

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

#ifndef BEEBIUM_TEST_ROM_DIR
#error "BEEBIUM_TEST_ROM_DIR must be defined"
#endif

using namespace beebium;
using namespace beebium::test;

namespace {

// Each coprocessor's client ROM now ships in its own plugin's roms/ directory;
// the tests read them from there via the compile-time BEEBIUM_TUBE_*_ROM_DIR.
static constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr const char* TUBE_65C102_ROM_FILENAME = "acorn-tube-65c102_1_20.rom";
static constexpr const char* DNFS_ROM_FILENAME = "acorn-dnfs_3_02.rom";
static constexpr size_t TUBE_ROM_SIZE = 4096;

bool tube_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME);
}

bool tube_65c102_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_TUBE_65C102_ROM_DIR) / TUBE_65C102_ROM_FILENAME);
}

bool dnfs_rom_available() {
    return std::filesystem::exists(
               std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME) ||
           std::filesystem::exists(
               std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / DNFS_ROM_FILENAME);
}

std::array<uint8_t, TUBE_ROM_SIZE> load_rom_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());

    std::array<uint8_t, TUBE_ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), TUBE_ROM_SIZE);
    REQUIRE(file.gcount() == static_cast<std::streamsize>(TUBE_ROM_SIZE));
    return rom;
}

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_rom() {
    return load_rom_file(std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME);
}

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_65c102_rom() {
    return load_rom_file(
        std::filesystem::path(BEEBIUM_TUBE_65C102_ROM_DIR) / TUBE_65C102_ROM_FILENAME);
}

// Set up a Model B with MOS + BASIC + DNFS (which includes Tube Host code).
// DNFS handles service call &FF, required for MOS Tube initialisation.
void setup_tube_machine(ModelB& machine) {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    // Load DNFS into slot 13 (standard DFS position).
    auto dnfs_filepath = rom_dirpath / DNFS_ROM_FILENAME;
    if (!std::filesystem::exists(dnfs_filepath)) {
        dnfs_filepath = std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / DNFS_ROM_FILENAME;
    }
    auto dnfs = load_rom(dnfs_filepath);
    machine.memory().load_sideways_rom(13, dnfs.data(), dnfs.size());
}

}  // namespace

// =============================================================================
// The coup de theatre: full host + coprocessor boot
// =============================================================================

TEST_CASE("Model B with 65C02 second processor boots with Tube banner",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!dnfs_rom_available()) SKIP("DNFS ROM not available");

    // --- Host setup ---
    ModelB machine;
    setup_tube_machine(machine);

    // Enable Tube socket BEFORE reset, so MOS sees it
    machine.state().memory.tube_socket.enable();

    // Reset the host
    machine.reset();

    // --- Coprocessor setup ---
    auto tube_rom = load_tube_rom();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    CoprocessorRunner coprocessor(*tube, tube_rom);
    coprocessor.reset();

    // Install the coprocessor to be driven in host time from Machine::step().
    // The 3:2 clock ratio (1.5 coprocessor cycles per host cycle) lives with the
    // runner, not the socket.
    machine.state().memory.tube_socket.install_coprocessor(&coprocessor);

    // --- Boot ---
    // Machine::step() now runs the coprocessor automatically via TubeSocket.
    machine.run(30'000'000);

    // --- Verify screen ---
    INFO("Screen:\n" << dump_screen(machine));

    // The Tube banner replaces the normal "BBC Computer 32K" header
    CHECK(screen_contains(machine, "Acorn TUBE 6502 64K"));

    // The normal "BBC Computer 32K" line should NOT appear
    CHECK_FALSE(screen_contains(machine, "BBC Computer 32K"));

    // BASIC must have entered and printed its chevron prompt
    CHECK(screen_contains(machine, ">"));
}

TEST_CASE("Model B with 65C102 4 MHz second processor boots and runs at 2x host",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_65c102_rom_available()) SKIP("Tube 65C102 ROM not available");
    if (!dnfs_rom_available()) SKIP("DNFS ROM not available");

    // The 65C102 is a 65C02-family second processor at 4 MHz (ratio 2/1). It
    // has its own client ROM with its own banner, distinct from the 65C02's.
    ModelB machine;
    setup_tube_machine(machine);
    machine.state().memory.tube_socket.enable();
    machine.reset();

    auto tube_rom = load_tube_65c102_rom();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    CoprocessorRunner coprocessor(*tube, tube_rom, ClockRatio{2, 1});
    coprocessor.reset();
    machine.state().memory.tube_socket.install_coprocessor(&coprocessor);

    // Capture the cycle origins right before running: the coprocessor's clock
    // origin is the host cycle at the first step after install.
    const uint64_t host_before = machine.state().cycle_count;
    const uint64_t coprocessor_before = coprocessor.cycle_count();

    machine.run(30'000'000);

    INFO("Screen:\n" << dump_screen(machine));

    // The 65C102's own banner (not the 6502's), proving its own ROM booted.
    CHECK(screen_contains(machine, "Acorn TUBE 65C102 Co-Processor"));
    CHECK_FALSE(screen_contains(machine, "BBC Computer 32K"));
    CHECK(screen_contains(machine, ">"));

    // Each step() drives the coprocessor to the host cycle at the START of the
    // step, so after the run it trails the host by the final cycle. Advance it
    // to the current host cycle -- exactly what the next step() would do -- so
    // the counters line up at the same host time.
    machine.state().memory.tube_socket.run_coprocessor_until(machine.state().cycle_count);

    // The coprocessor ran exactly twice the host's cycles over the boot -- the 2/1
    // ratio, measured by the cycle counters, not wall time.
    const uint64_t host_cycles = machine.state().cycle_count - host_before;
    const uint64_t coprocessor_cycles = coprocessor.cycle_count() - coprocessor_before;
    CHECK(coprocessor_cycles == 2 * host_cycles);
}

TEST_CASE("Model B with Tube shows 64K memory (not 32K)",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!tube_rom_available()) SKIP("Tube 6502 ROM not available");
    if (!dnfs_rom_available()) SKIP("DNFS ROM not available");

    ModelB machine;
    setup_tube_machine(machine);

    machine.state().memory.tube_socket.enable();
    machine.reset();

    auto tube_rom = load_tube_rom();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    CoprocessorRunner coprocessor(*tube, tube_rom);
    coprocessor.reset();

    machine.state().memory.tube_socket.install_coprocessor(&coprocessor);

    machine.run(30'000'000);

    INFO("Screen:\n" << dump_screen(machine));

    // The banner includes "64K" (the coprocessor's memory size)
    CHECK(screen_contains(machine, "64K"));
}

TEST_CASE("Model B without Tube boots normally",
          "[boot][tube]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");

    ModelB machine;
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    machine.reset();
    for (uint64_t i = 0; i < 3'500'000; ++i) {
        machine.step_instruction();
    }

    INFO("Screen:\n" << dump_screen(machine));

    CHECK(screen_contains(machine, "BBC Computer 32K"));
    CHECK(screen_contains(machine, "BASIC"));
    CHECK_FALSE(screen_contains(machine, "Acorn TUBE"));
}

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
// This is the full-stack integration test: a host Model B and a parasite
// 65C02 share a TubeUla, both run their real boot ROMs, and the test
// verifies that the parasite's banner ("Acorn TUBE 6502 64K") appears
// on the host's MODE 7 screen.
//
// The boot sequence:
//   1. Host resets, MOS starts initialisation
//   2. Parasite boots from its 2 KB client ROM, fills R1 P-to-H FIFO
//      with the banner string (24 bytes)
//   3. Host MOS writes &81 to &FEE0 (set Q), reads back, detects Tube ULA
//   4. Host MOS issues service call &FF -- DNFS ROM handles this and
//      sets the Tube presence flag at &027A
//   5. Host MOS reads the banner from R1 and prints it on screen
//   6. Host MOS attempts language transfer via R2/R4 (parasite responds)
//
// The DNFS ROM is required because MOS issues service call &FF after
// detecting Tube hardware. Without a ROM that claims this call, the
// Tube presence flag is never set, and MOS ignores the Tube.
//
// The parasite is installed on the TubeSocket and ticked automatically
// by Machine::step() at a 3:2 clock ratio (3 MHz parasite / 2 MHz host).

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/tube/ParasiteRunner.hpp>
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

static constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr const char* DNFS_ROM_FILENAME = "acorn-dnfs_3_02.rom";
static constexpr size_t TUBE_ROM_SIZE = 2048;

bool tube_rom_available() {
    return std::filesystem::exists(
        std::filesystem::path(BEEBIUM_ROM_DIR) / TUBE_ROM_FILENAME);
}

bool dnfs_rom_available() {
    return std::filesystem::exists(
               std::filesystem::path(BEEBIUM_ROM_DIR) / DNFS_ROM_FILENAME) ||
           std::filesystem::exists(
               std::filesystem::path(BEEBIUM_TEST_ROM_DIR) / DNFS_ROM_FILENAME);
}

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_ROM_DIR) / TUBE_ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());

    std::array<uint8_t, TUBE_ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), TUBE_ROM_SIZE);
    REQUIRE(file.gcount() == static_cast<std::streamsize>(TUBE_ROM_SIZE));
    return rom;
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
// The coup de theatre: full host + parasite boot
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

    // --- Parasite setup ---
    auto tube_rom = load_tube_rom();
    TubeUla* tube = machine.state().memory.tube_socket.tube_ula();
    REQUIRE(tube != nullptr);
    ParasiteRunner parasite(*tube, tube_rom);
    parasite.reset();

    // Install the coprocessor to be driven in host time from Machine::step().
    // The 3:2 clock ratio (1.5 parasite cycles per host cycle) lives with the
    // runner, not the socket.
    machine.state().memory.tube_socket.install_coprocessor(&parasite);

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
    ParasiteRunner parasite(*tube, tube_rom);
    parasite.reset();

    machine.state().memory.tube_socket.install_coprocessor(&parasite);

    machine.run(30'000'000);

    INFO("Screen:\n" << dump_screen(machine));

    // The banner includes "64K" (the parasite's memory size)
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

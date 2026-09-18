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

// Whole-machine regression guard for issue #81: Dominic Plunkett's (dp111) 6502
// instruction timing suite, checked through the result register it documents.
//
// The suite (github.com/dp111/6502Timing, GPL-3.0, version 0.24) ends by writing
// its failure count to &FCD0, "so emulators can trap writes to this address".
// &FCD0 is in FRED on the 1 MHz bus, which is open-bus and reads back 0xFF, so a
// C++ test observes the write with a WATCH_WRITE watchpoint rather than by peeking
// memory or scraping the screen. A single write of 0 is a clean pass across every
// documented and undocumented NMOS instruction, the System VIA timer and -- for
// the 1M image, whose timed absolute addresses sit at &FCFE -- the 1 MHz bus cycle
// stretching. Both images pass on master today, so this guards existing behaviour;
// it does not reproduce a defect.
//
// The disc autoboots (its !Boot is "*BASIC" then "*RUN 6502tim", run via boot
// option 3); set_auto_boot(true) reverses the SHIFT-BREAK keyboard link so a plain
// reset boots the disc, the C++ equivalent of holding SHIFT across BREAK.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <beebium/Machines.hpp>
#include <beebium/Types.hpp>
#include <beebium/disc/DiscLoader.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "test_econet_helpers.hpp"

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

using namespace beebium;
using namespace beebium::test;

namespace {

constexpr const char* DFS_ROM_FILENAME = "acorn-dfs_2_26.rom";
constexpr uint16_t PASS_FAIL_REG = 0xFCD0;  // suite writes its failure count here

bool files_available(const std::string& disc_filename) {
    auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom")
        && std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom")
        && std::filesystem::exists(rom_dirpath / DFS_ROM_FILENAME)
        && std::filesystem::exists(assets_dirpath / "discs" / disc_filename);
}

struct SuiteResult {
    bool completed = false;   // the suite wrote its failure count to &FCD0
    uint8_t failures = 0xFF;
    uint64_t cycles = 0;
    std::string screen;
};

// Autoboot the dp111 timing suite on a Model B + 1770 DFS and trap the &FCD0 write.
SuiteResult run_timing_suite(const std::string& disc_filename,
                             uint64_t max_cycles = 40'000'000) {
    auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);

    ModelB machine;
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto dfs = load_rom(rom_dirpath / DFS_ROM_FILENAME);
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
    machine.memory().install_acorn_1770();

    auto disc_filepath = assets_dirpath / "discs" / disc_filename;
    auto disc_result = load_disc_from_url_or_filepath(disc_filepath.string());
    REQUIRE(disc_result.success());
    machine.memory().disc_drive_0.insert(std::move(disc_result.disc));

    machine.memory().set_auto_boot(true);
    machine.reset();

    SuiteResult result;
    machine.set_watchpoint_hit_callback(
        [&result](const WatchpointEntry&, uint32_t addr, uint8_t value, bool is_write) {
            if (is_write && addr == PASS_FAIL_REG) {
                result.completed = true;
                result.failures = value;
            }
        });
    machine.add_watchpoint_entry(
        WatchpointEntry{/*id*/ 1, /*start*/ PASS_FAIL_REG, /*end*/ PASS_FAIL_REG + 1,
                        WATCH_WRITE});

    while (result.cycles < max_cycles && !result.completed) {
        machine.step();
        ++result.cycles;
    }

    result.screen = dump_screen(machine);
    return result;
}

}  // namespace

TEST_CASE("dp111 timing suite writes 0 failures to &FCD0", "[6502][dp111][timing][disc]") {
    const std::string disc_filename =
        GENERATE(std::string("6502timing.ssd"), std::string("6502timing1M.ssd"));

    if (!files_available(disc_filename)) {
        SKIP("ROMs or disc image not available: " << disc_filename);
    }

    SuiteResult result = run_timing_suite(disc_filename);
    INFO("disc=" << disc_filename << " cycles=" << result.cycles
                 << " completed=" << (result.completed ? "yes" : "no")
                 << " failures=" << static_cast<int>(result.failures) << "\n"
                 << result.screen);
    REQUIRE(result.completed);
    REQUIRE(result.failures == 0);
}

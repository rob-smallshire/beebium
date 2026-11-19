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

// The Master 128's timing oracle: Dominic Plunkett's (dp111) instruction
// timing suite in its 65C12 build (tests/assets/discs/README.md), run on the
// Master 128 with MOS 3.20, its 1770 and DFS 2.24. The suite times every
// documented and undocumented 65C12 instruction against System VIA timer 1,
// with its timed absolute addresses at &FCFE straddling the 1 MHz page (FRED
// on the external bus, IFJ clear), and writes its failure count to &FCD0,
// which a WATCH_WRITE watchpoint traps (as test_dp111_timing_fcd0.cpp does on
// the Model B). Research section 5 item 7 records the results.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/disc/DiscLoader.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif
#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

using namespace beebium;

namespace {

const std::filesystem::path ROM_DIRPATH = BEEBIUM_ROM_DIR;
const std::filesystem::path DISC_FILEPATH =
    std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "discs" / "65C12timing1M.ssd";
constexpr uint16_t PASS_FAIL_REG = 0xFCD0;  // the suite writes its failure count here

std::vector<uint8_t> read_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool files_available() {
    if (!std::filesystem::exists(DISC_FILEPATH)) return false;
    if (!std::filesystem::exists(ROM_DIRPATH / Master128Hardware::DEFAULT_MOS_ROM)) return false;
    for (const auto& rom : Master128Hardware::DEFAULT_SIDEWAYS_ROMS) {
        if (!std::filesystem::exists(ROM_DIRPATH / rom.image_filename)) return false;
    }
    return std::filesystem::exists(ROM_DIRPATH / Master128Hardware::DEFAULT_LANGUAGE_ROM);
}

// The Mode 7 screen as text, from the CRTC's start address in main RAM.
std::string mode7_screen(Master128& machine) {
    auto& hw = machine.memory();
    const uint16_t start = static_cast<uint16_t>(
        0x7C00 | ((hw.crtc.screen_start() & 0x03FF)));
    std::ostringstream out;
    for (int row = 0; row < 25; ++row) {
        for (int column = 0; column < 40; ++column) {
            const auto addr = static_cast<uint16_t>(0x7C00 | ((start + row * 40 + column) & 0x03FF));
            const uint8_t c = hw.main_ram.read(addr) & 0x7F;
            out << (c >= 0x20 && c < 0x7F ? static_cast<char>(c) : ' ');
        }
        out << '\n';
    }
    return out.str();
}

}  // namespace

TEST_CASE("Master 128: the dp111 65C12 timing suite writes 0 failures to &FCD0",
          "[master128][dp111][timing][disc][rom]") {
    if (!files_available()) SKIP("Master 128 ROMs or the 65C12 timing disc not available");

    auto machine = std::make_unique<Master128>();
    auto& hw = machine->memory();
    const auto mos = read_file(ROM_DIRPATH / Master128Hardware::DEFAULT_MOS_ROM);
    hw.load_mos(mos.data(), mos.size());
    const auto basic = read_file(ROM_DIRPATH / Master128Hardware::DEFAULT_LANGUAGE_ROM);
    hw.load_sideways_rom(Master128Hardware::DEFAULT_LANGUAGE_SLOT, basic.data(), basic.size());
    for (const auto& rom : Master128Hardware::DEFAULT_SIDEWAYS_ROMS) {
        const auto image = read_file(ROM_DIRPATH / rom.image_filename);
        hw.load_sideways_rom(rom.slot, image.data(), image.size());
    }
    auto disc = load_disc_from_url_or_filepath(DISC_FILEPATH.string());
    REQUIRE(disc.success());
    hw.disc_drive_0.insert(std::move(disc.disc));
    machine->reset();

    bool completed = false;
    uint8_t failures = 0xFF;
    machine->set_watchpoint_hit_callback(
        [&](const WatchpointEntry&, uint32_t addr, uint8_t value, bool is_write) {
            if (is_write && addr == PASS_FAIL_REG && !completed) {
                completed = true;
                failures = value;
            }
        });
    machine->add_watchpoint_entry(
        WatchpointEntry{1, PASS_FAIL_REG, PASS_FAIL_REG + 1, WATCH_WRITE});

    // Boot to BASIC, then run the suite from DFS (the seed's filing system
    // is ADFS).
    for (uint64_t i = 0; i < 6'000'000; ++i) machine->step();
    REQUIRE(hw.system_via_peripheral.type_ahead().enqueue("*DISC\r*RUN 6502tim\r"));

    uint64_t cycles = 0;
    while (!completed && cycles < 60'000'000) {
        machine->step();
        ++cycles;
    }
    const auto screen = mode7_screen(*machine);
    INFO("cycles=" << cycles << " failures=" << static_cast<int>(failures) << "\n" << screen);
    REQUIRE(completed);
    CHECK(failures == 0);
}

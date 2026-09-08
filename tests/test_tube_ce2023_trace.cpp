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

// CE2023 full-boot regression test.
//
// Boots Chuckie Egg 2023 with a Model B + 65C02 Tube using interleaved
// execution and verifies that the game loads successfully (the coprocessor
// does not hang at the R1 poll loop).
//
// This test was the primary reproduction case during the CE2023
// investigation. See docs/discussion/chuckie-egg-2023-tube-hang.md.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/disc/DiscLoader.hpp>
#include <beebium/tube/CoprocessorRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>

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

static constexpr const char* TUBE_ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr const char* DFS_ROM_FILENAME = "acorn-dfs_2_26.rom";
static constexpr const char* DISC_FILENAME = "chuckieEgg2023.ssd";
static constexpr size_t TUBE_ROM_SIZE = 2048;

// The R1 poll loop address where the coprocessor hangs if the
// decompressor runs out of data.
static constexpr uint16_t DECOMP_R1_BPL = 0x09D4;

bool files_available() {
    auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);
    return std::filesystem::exists(rom_dirpath / "acorn-mos_1_20.rom")
        && std::filesystem::exists(rom_dirpath / "bbc-basic_2.rom")
        && std::filesystem::exists(rom_dirpath / DFS_ROM_FILENAME)
        && std::filesystem::exists(
               std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME)
        && std::filesystem::exists(assets_dirpath / "discs" / DISC_FILENAME);
}

std::array<uint8_t, TUBE_ROM_SIZE> load_tube_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / TUBE_ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());
    std::array<uint8_t, TUBE_ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), TUBE_ROM_SIZE);
    REQUIRE(file.gcount() == static_cast<std::streamsize>(TUBE_ROM_SIZE));
    return rom;
}

struct TestFixture {
    TubeUla tube;
    ModelB machine;
    std::unique_ptr<CoprocessorRunner> coprocessor;
    HeapFrameAllocator allocator;
    FrameBuffer fb;
    FrameRenderer renderer;

    TestFixture()
        : fb(&allocator, 640, 512)
        , renderer(&fb)
    {
        auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
        auto assets_dirpath = std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR);

        auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
        auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
        auto dfs = load_rom(rom_dirpath / DFS_ROM_FILENAME);
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
        machine.memory().install_acorn_1770();

        auto disc_filepath = assets_dirpath / "discs" / DISC_FILENAME;
        auto disc_result = load_disc_from_url_or_filepath(disc_filepath.string());
        REQUIRE(disc_result.success());
        machine.memory().disc_drive_0.insert(std::move(disc_result.disc));

        machine.state().memory.tube_socket.install_backend(&tube);
        machine.memory().enable_video_output();
        machine.memory().set_auto_boot(true);
        machine.reset();

        auto tube_rom = load_tube_rom();
        coprocessor = std::make_unique<CoprocessorRunner>(tube, tube_rom);
        coprocessor->reset();
    }

    // Run interleaved with given batch sizes.
    // Returns true if hang detected, false if budget exhausted.
    bool run_until_hang(int host_batch, int coprocessor_batch, int max_rounds) {
        int poll_count = 0;

        for (int round = 0; round < max_rounds; ++round) {
            for (int i = 0; i < host_batch; ++i) {
                machine.step();
                if (machine.memory().video_output.has_value())
                    renderer.process(machine.memory().video_output.value());
            }
            for (int i = 0; i < coprocessor_batch; ++i)
                coprocessor->step_instruction();

            // Check for hang periodically
            if ((round & 0x3FF) == 0) {
                if (coprocessor->pc() == DECOMP_R1_BPL) {
                    if (++poll_count > 1000) return true;
                } else {
                    poll_count = 0;
                }
            }
        }
        return false;
    }
};

}  // namespace

TEST_CASE("CE2023 loads with 1:1 interleaved execution", "[tube][ce2023]") {
    if (!files_available()) SKIP("Required ROMs or disc image not available");

    TestFixture fix;

    bool hung = fix.run_until_hang(1, 1, 60'000'000);

    CHECK_FALSE(hung);

    if (hung) {
        FAIL("CE2023 hung at R1 poll ($09D4) -- page-cross fixup read regression?");
    }
}

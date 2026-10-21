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

// The 8271-era Acorn DFS releases on a Model B with the Acorn 8271 board:
// each boots, catalogues a disc, and saves and loads a file. The releases
// poll the drive differently, so each is run.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/Machines.hpp>
#include <beebium/disc/DiscControllerRegistry.hpp>
#include <beebium/disc/DiscLoader.hpp>

#include "test_keyboard_helpers.hpp"
#include "test_temp_paths.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace beebium;
using namespace beebium::test;

namespace {

std::vector<uint8_t> load_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool screen_contains(ModelB& machine, const std::string& text) {
    for (int row = 0; row < 25; ++row) {
        if (read_mode7_screen(machine, row, 0, 40).find(text) != std::string::npos) return true;
    }
    return false;
}

std::string screen_dump(ModelB& machine) {
    std::string dump;
    for (int row = 0; row < 25; ++row) dump += read_mode7_screen(machine, row, 0, 40) + "\n";
    return dump;
}

struct DfsMachine {
    ModelB machine;
    HeapFrameAllocator allocator;
    FrameBuffer frame_buffer{&allocator, 640, 512};
    FrameRenderer renderer{&frame_buffer};

    explicit DfsMachine(const std::string& dfs_filename, const std::filesystem::path& disc_filepath) {
        const std::filesystem::path rom_dirpath(BEEBIUM_ROM_DIR);
        auto mos = load_file(rom_dirpath / "acorn-mos_1_20.rom");
        auto basic = load_file(rom_dirpath / "bbc-basic_2.rom");
        auto dfs = load_file(rom_dirpath / dfs_filename);
        REQUIRE(mos.size() == 16384);
        REQUIRE_FALSE(dfs.empty());

        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_sideways_rom(15, basic.data(), basic.size());
        machine.memory().load_sideways_rom(14, dfs.data(), dfs.size());
        machine.memory().install_disc_controller(DiscControllerRegistry::create("acorn-8271"),
                                                  "acorn-8271");
        auto result = load_disc_from_url_or_filepath(disc_filepath.string());
        REQUIRE(result.success());
        machine.memory().disc_drive_0.insert(std::move(result.disc));

        machine.memory().enable_video_output();
        machine.reset();
    }

    void run(uint64_t cycles) {
        for (uint64_t i = 0; i < cycles; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    void type(const char* text) { type_string_with_shift(machine, renderer, text, 100000); }

    // Run until the text appears on screen, up to a limit
    bool run_until_on_screen(const std::string& text, uint64_t max_cycles) {
        for (uint64_t done = 0; done < max_cycles; done += 100'000) {
            if (screen_contains(machine, text)) return true;
            run(100'000);
        }
        return screen_contains(machine, text);
    }
};

}  // namespace

TEST_CASE("8271 DFS releases boot, catalogue, save and load", "[disc][i8271][dfs][integration]") {
    auto dfs_filename = GENERATE(as<std::string>{},
        "acorn-dfs_0_90.rom",      // DFS 0.90
        "acorn-dfs_0_98.rom",      // DFS 0.98
        "acorn-dnfs_1_00.rom",     // DFS 1.00 + NFS 3.40
        "acorn-dnfs_1_20.rom",     // DFS 1.20 + NFS 3.60
        "acorn-dnfs_3_02.rom");    // DFS 1.22 + NFS 3.62
    INFO("ROM: " << dfs_filename);

    // A private copy of the disc, since the test writes to it
    ScopedTempDir temp_dir("beebium-8271-dfs-");
    auto disc_filepath = temp_dir / "boffin.ssd";
    std::filesystem::copy_file(std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "discs" / "Disc016-Boffin.ssd",
                               disc_filepath);

    DfsMachine dfs(dfs_filename, disc_filepath);
    dfs.run(4'000'000);
    INFO(screen_dump(dfs.machine));
    REQUIRE(screen_contains(dfs.machine, "DFS"));
    REQUIRE(screen_contains(dfs.machine, "BASIC"));

    dfs.type("*CAT\r");
    REQUIRE(dfs.run_until_on_screen("BOFFIN1", 20'000'000));
    INFO(screen_dump(dfs.machine));
    CHECK(screen_contains(dfs.machine, "!BOOT"));

    // Save a page of memory, clear it, load it back.
    for (uint16_t i = 0; i < 256; ++i) dfs.machine.write(static_cast<uint16_t>(0x3000 + i), static_cast<uint8_t>(i ^ 0x5A));
    dfs.type("*SAVE TEST 3000 3100\r");
    dfs.run(10'000'000);
    for (uint16_t i = 0; i < 256; ++i) dfs.machine.write(static_cast<uint16_t>(0x3000 + i), 0);
    dfs.type("*LOAD TEST\r");
    dfs.run(10'000'000);
    INFO(screen_dump(dfs.machine));
    for (uint16_t i = 0; i < 256; ++i) {
        CHECK(dfs.machine.peek(static_cast<uint16_t>(0x3000 + i)) == static_cast<uint8_t>(i ^ 0x5A));
    }
}

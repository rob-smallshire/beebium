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

// test_rom_paths.cpp
// Tests for finding a ROM by the file reference rule and reading it through
// the launch's container (RomPaths.hpp).

#include <beebium/server/RomPaths.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace beebium::server;
namespace fs = std::filesystem;

namespace {

// A container, a per-user directory and a system ROM directory, all empty.
struct RomTree {
    beebium::test::ScopedTempDir root{"beebium_rom_paths_"};
    fs::path container = root / "container";
    fs::path user = root / "user";
    fs::path system = root / "system";
    beebium::test::ScopedEnv user_env{"BEEBIUM_USER_DIRPATH", user.string()};

    RomTree() {
        for (const auto& d : {container / "sub", user / "roms", system}) {
            fs::create_directories(d);
        }
        RomPaths::set_rom_directory(system);
    }
    ~RomTree() { RomPaths::clear_rom_directory(); }

    // A 16K ROM filled with `fill`, so each copy can be told apart.
    static void put_rom(const fs::path& filepath, char fill = '\xff') {
        std::ofstream(filepath, std::ios::binary) << std::string(16384, fill);
    }
};

}  // namespace

TEST_CASE("find_rom: a bare name is found in the container, then per-user, then system",
          "[rom_paths]") {
    RomTree t;
    RomTree::put_rom(t.system / "x.rom");
    CHECK(RomPaths::find_rom("x.rom", t.container) == t.system / "x.rom");
    RomTree::put_rom(t.user / "roms" / "x.rom");
    CHECK(RomPaths::find_rom("x.rom", t.container) == t.user / "roms" / "x.rom");
    RomTree::put_rom(t.container / "x.rom");
    CHECK(RomPaths::find_rom("x.rom", t.container) == t.container / "x.rom");
    CHECK_THROWS_WITH(RomPaths::find_rom("nope.rom", t.container),
                      Catch::Matchers::ContainsSubstring("ROM 'nope.rom' not found; searched " +
                                                         t.container.string()));
}

TEST_CASE("find_rom: a path is anchored in the container, or home", "[rom_paths]") {
    RomTree t;
    RomTree::put_rom(t.container / "sub" / "x.rom");
    CHECK(RomPaths::find_rom("sub/x.rom", t.container) ==
          (t.container / "sub" / "x.rom").lexically_normal());
    CHECK(RomPaths::find_rom((t.container / "sub" / "x.rom").string(), t.root.path()) ==
          t.container / "sub" / "x.rom");
    beebium::test::ScopedEnv home("HOME", t.container.string());
    CHECK(RomPaths::find_rom("~/sub/x.rom", t.root.path()) ==
          (t.container / "sub" / "x.rom").lexically_normal());
    CHECK_THROWS_WITH(RomPaths::find_rom("sub/missing.rom", t.container),
                      Catch::Matchers::ContainsSubstring("ROM file not found: "));
}

TEST_CASE("load_rom: the ROM's file and its bytes, read through the container", "[rom_paths]") {
    RomTree t;
    RomTree::put_rom(t.system / "x.rom", 'S');
    RomTree::put_rom(t.container / "sub" / "y.rom", 'C');

    const auto from_system = RomPaths::load_rom("x.rom", t.container);
    CHECK(from_system.filepath == t.system / "x.rom");
    CHECK(from_system.data == std::vector<std::uint8_t>(16384, 'S'));

    const auto from_container = RomPaths::load_rom("sub/y.rom", t.container);
    CHECK(from_container.filepath == (t.container / "sub" / "y.rom").lexically_normal());
    CHECK(from_container.data == std::vector<std::uint8_t>(16384, 'C'));

    CHECK_THROWS(RomPaths::load_rom("nope.rom", t.container));
}

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

// The Tube client ROM is the full 4 KB 2732 device, mapped at &F000-&FFFF. Its
// lower half (&F000-&F7FF) is genuine ROM address space -- Acorn's own firmware
// leaves it blank, but a client such as ReCo6502 executes from it. This test
// drives a synthetic 4 KB client image whose reset vector points into the lower
// half through the *real* coprocessor extension, and proves the coprocessor
// executed code from there: it writes a marker to its own RAM, which we read
// back through the debug target.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/extension/CpuDebugTarget.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include "SecondProcessor65C02Extension.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace beebium;

namespace {

std::filesystem::path write_temp_rom(const std::array<uint8_t, 4096>& rom) {
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path()
             / ("beebium-4k-rom-" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    auto path = dir / "client.rom";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(rom.data()),
            static_cast<std::streamsize>(rom.size()));
    return path;
}

}  // namespace

TEST_CASE("A 4 kB client image executes from its lower half through the extension",
          "[coprocessor][rom][4k]") {
    // Synthetic client: reset vector -> &F000 (the lower half), where a short
    // program stores a marker byte into RAM and then spins. If the lower half
    // were not mapped ROM, the CPU would fetch garbage and never write the
    // marker.
    std::array<uint8_t, 4096> rom{};
    rom.fill(0xFF);
    const uint8_t program[] = {
        0xA9, 0x5A,        // &F000  LDA #$5A
        0x85, 0x40,        // &F002  STA $40
        0xAD, 0xF8, 0xFE,  // &F004  LDA $FEF8   (touch Tube R1 -> leaves boot mode)
        0x4C, 0x07, 0xF0,  // &F007  JMP $F007   (spin)
    };
    for (size_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];  // at &F000
    rom[0xFFC] = 0x00;  // reset vector low  -> &F000
    rom[0xFFD] = 0xF0;  // reset vector high

    const auto rom_path = write_temp_rom(rom);

    // Wire the real extension to a host machine and boot it.
    ModelB machine;
    ExtensionContext ctx(nullptr, nullptr, &machine.state().memory.tube_socket);
    SecondProcessor65C02Extension ext;
    ext.set_config({{"id", "lowerhalf"}, {"rom", rom_path.string()}});
    ext.init(ctx);
    REQUIRE(ext.running());
    machine.reset();

    CpuDebugTarget* target = ext.debug_target();
    REQUIRE(target != nullptr);

    // Run the host; Machine::step drives the coprocessor in host time, so a few
    // thousand host cycles is ample for the handful of client instructions.
    for (int i = 0; i < 100; ++i) machine.run(2000);

    // The marker proves the coprocessor executed the program located in the
    // lower half of the 4 KB ROM.
    CHECK(target->peek_region("ram", 0x0040) == 0x5A);

    std::filesystem::remove_all(rom_path.parent_path());
}

TEST_CASE("The extension rejects a wrong-size client ROM naming the device",
          "[coprocessor][rom][4k]") {
    // A 2 KB file -- the upper-half-only image other emulators ship -- is a
    // fragment of the 2732, not the device image, and is refused.
    std::vector<uint8_t> half(2048, 0xFF);
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path()
             / ("beebium-4k-rom-bad-" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    auto path = dir / "half.rom";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(half.data()),
                static_cast<std::streamsize>(half.size()));
    }

    ModelB machine;
    ExtensionContext ctx(nullptr, nullptr, &machine.state().memory.tube_socket);
    SecondProcessor65C02Extension ext;
    ext.set_config({{"id", "bad"}, {"rom", path.string()}});

    // init() fails to load the ROM, so the runner is never created.
    try {
        ext.init(ctx);
    } catch (const std::exception&) {
        // Some builds surface the failure as an exception from init(); either
        // way the extension must not come up running.
    }
    CHECK_FALSE(ext.running());

    std::filesystem::remove_all(dir);
}

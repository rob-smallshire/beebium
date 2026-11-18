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

// A key held across Break, then released, leaves the keyboard readable: a key
// pressed afterwards reads as held through OSBYTE &81 with a negative INKEY
// number, which scans the matrix directly through the System VIA.

#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/Machines.hpp>
#include <beebium/TeletextGrid.hpp>
#include <beebium/TeletextText.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace beebium;

namespace {

const std::filesystem::path ROM_DIRPATH = BEEBIUM_ROM_DIR;

std::vector<uint8_t> read_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool roms_present() {
    return std::filesystem::exists(ROM_DIRPATH / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(ROM_DIRPATH / "bbc-basic_2.rom");
}

constexpr uint16_t PROGRAM = 0x0900;
constexpr uint16_t RESULT = 0x0070;   // X from the last OSBYTE &81
constexpr uint16_t PASSES = 0x0071;   // incremented each pass

// Loop for ever: OSBYTE &81 with X = &9D, Y = &FF (INKEY-99, SPACE), store
// X in &70, count the pass in &71.
const std::vector<uint8_t> program = {
    0xA9, 0x81,        // LDA #&81
    0xA2, 0x9D,        // LDX #&9D
    0xA0, 0xFF,        // LDY #&FF
    0x20, 0xF4, 0xFF,  // JSR OSBYTE
    0x86, 0x70,        // STX &70
    0xE6, 0x71,        // INC &71
    0x4C, 0x00, 0x09,  // JMP &0900
};

constexpr uint8_t SPACE_ROW = 6, SPACE_COLUMN = 2;

struct Beeb {
    ModelB machine;
    HeapFrameAllocator allocator;
    FrameBuffer frame_buffer{&allocator, 640, 512};
    FrameRenderer renderer{&frame_buffer};
    TeletextGrid grid;

    Beeb() {
        const auto mos = read_file(ROM_DIRPATH / "acorn-mos_1_20.rom");
        const auto basic = read_file(ROM_DIRPATH / "bbc-basic_2.rom");
        machine.memory().load_mos(mos.data(), mos.size());
        machine.memory().load_basic(basic.data(), basic.size());
        machine.memory().saa5050.set_teletext_grid(&grid);
        machine.memory().enable_video_output();
        machine.reset();
    }

    void step(uint64_t steps) {
        for (uint64_t i = 0; i < steps; ++i) {
            machine.step();
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }
    }

    void seconds(double s) { step(static_cast<uint64_t>(s * 2'000'000.0)); }

    void key_down(uint8_t row, uint8_t column) {
        machine.memory().system_via_peripheral.key_down(row, column);
    }
    void key_up(uint8_t row, uint8_t column) {
        machine.memory().system_via_peripheral.key_up(row, column);
    }

    void type(const std::string& text) {
        REQUIRE(machine.memory().system_via_peripheral.type_ahead().enqueue(text));
    }

    std::string screen() { return teletext_text(grid); }

    void load_program() {
        for (size_t i = 0; i < program.size(); ++i) {
            machine.write(static_cast<uint16_t>(PROGRAM + i), program[i]);
        }
    }

    uint8_t peek(uint16_t address) { return machine.memory().peek(address); }

    // Start the loop and check SPACE reads as up, then as held once pressed.
    void check_space_reads(bool expect_held_after_press) {
        load_program();
        // A RETURN first ends any line the held key repeated into.
        type("\rCALL &900\r");
        seconds(2.0);
        const uint8_t passes_before = peek(PASSES);
        seconds(0.1);
        REQUIRE(peek(PASSES) != passes_before);   // the loop runs
        CHECK(peek(RESULT) == 0x00);              // SPACE is up

        key_down(SPACE_ROW, SPACE_COLUMN);
        seconds(0.1);
        CHECK((peek(RESULT) == 0xFF) == expect_held_after_press);
        key_up(SPACE_ROW, SPACE_COLUMN);
    }
};

struct HeldKey {
    const char* name;
    uint8_t row;
    uint8_t column;
};

}  // namespace

TEST_CASE("SPACE reads as held through OSBYTE &81 after boot", "[keyboard][reset]") {
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    Beeb beeb;
    beeb.seconds(2.0);
    REQUIRE(beeb.screen().find(">") != std::string::npos);
    beeb.check_space_reads(true);
}

TEST_CASE("SPACE reads as held through OSBYTE &81 after a key held across Break",
          "[keyboard][reset]") {
    if (!roms_present()) SKIP("The MOS 1.20 and BASIC 2 ROMs are not present.");
    for (const HeldKey held : {HeldKey{"A", 4, 1}, HeldKey{"Z", 6, 1}, HeldKey{"RETURN", 4, 9},
                               HeldKey{"SHIFT", 0, 0}, HeldKey{"CTRL", 0, 1},
                               HeldKey{"SPACE", SPACE_ROW, SPACE_COLUMN}}) {
        DYNAMIC_SECTION("held " << held.name) {
            Beeb beeb;
            beeb.seconds(2.0);
            REQUIRE(beeb.screen().find(">") != std::string::npos);

            beeb.key_down(held.row, held.column);
            beeb.seconds(0.1);
            beeb.machine.break_down();
            beeb.seconds(0.05);
            beeb.machine.break_up();
            beeb.seconds(1.5);
            beeb.key_up(held.row, held.column);
            beeb.seconds(1.0);
            INFO(beeb.screen());
            REQUIRE(beeb.screen().find(">") != std::string::npos);

            beeb.check_space_reads(true);
        }
    }
}

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

// The 8271 board in a Model B: a 6502 program drives the chip through
// &FE80-&FE87 and moves each byte in an NMI handler, as Acorn DFS does.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/disc/Intel8271DiscController.hpp>
#include <beebium/disc/formats/SsdFormatHandler.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace beebium;

namespace {

constexpr uint16_t RESET_HANDLER = 0x0400;
constexpr uint16_t NMI_HANDLER = 0x0D00;
constexpr uint16_t RESULT = 0x0071;
constexpr uint16_t BUFFER = 0x2000;

// Initialise the 8271 as DFS does, load drive 0's head and wait for ready,
// read track 0 sector 3 (one 256-byte sector), then store the result.
const std::vector<uint8_t> reset_handler = {
    0xA2, 0x00, 0x86, 0x72,                          // LDX #0 : STX &72
    0xA9, 0x3A, 0x8D, 0x80, 0xFE,                    // WRITE SPECIAL REGISTER
    0xA9, 0x17, 0x8D, 0x81, 0xFE,                    //   mode
    0xA9, 0xC1, 0x8D, 0x81, 0xFE,                    //   non-DMA
    0xA9, 0x35, 0x8D, 0x80, 0xFE,                    // SPECIFY
    0xA9, 0x0D, 0x8D, 0x81, 0xFE,                    //   timings
    0xA9, 0x03, 0x8D, 0x81, 0xFE,
    0xA9, 0x08, 0x8D, 0x81, 0xFE,
    0xA9, 0xF0, 0x8D, 0x81, 0xFE,
    0xA9, 0x35, 0x8D, 0x80, 0xFE,                    // SPECIFY
    0xA9, 0x10, 0x8D, 0x81, 0xFE,                    //   drive 0 tracks
    0xA9, 0xFF, 0x8D, 0x81, 0xFE,
    0xA9, 0xFF, 0x8D, 0x81, 0xFE,
    0xA9, 0x00, 0x8D, 0x81, 0xFE,
    0xA9, 0x3A, 0x8D, 0x80, 0xFE,                    // WRITE SPECIAL REGISTER
    0xA9, 0x23, 0x8D, 0x81, 0xFE,                    //   drive output
    0xA9, 0x48, 0x8D, 0x81, 0xFE,                    //   drive 0, load head
    0xA9, 0x6C, 0x8D, 0x80, 0xFE,                    // &0454: READ DRIVE STATUS
    0xAD, 0x81, 0xFE, 0x29, 0x04, 0xF0, 0xF4,        //   until ready
    0xA9, 0x53, 0x8D, 0x80, 0xFE,                    // READ DATA, drive 0
    0xA9, 0x00, 0x8D, 0x81, 0xFE,                    //   track 0
    0xA9, 0x03, 0x8D, 0x81, 0xFE,                    //   sector 3
    0xA9, 0x21, 0x8D, 0x81, 0xFE,                    //   one 256-byte sector
    0x2C, 0x80, 0xFE, 0x30, 0xFB,                    // &0474: BIT &FE80 : BMI
    0xAD, 0x81, 0xFE, 0x85, 0x71,                    // LDA &FE81 : STA &71
    0x4C, 0x7E, 0x04,                                // &047E: JMP &047E
};

// Move one byte to &2000+(&72) on a data request; otherwise leave the
// completion for the main program.
const std::vector<uint8_t> nmi_handler = {
    0x48,                    // PHA
    0xAD, 0x80, 0xFE,        // LDA &FE80
    0x29, 0x04,              // AND #4
    0xF0, 0x0E,              // BEQ done
    0x8A, 0x48,              // TXA : PHA
    0xA6, 0x72,              // LDX &72
    0xAD, 0x84, 0xFE,        // LDA &FE84
    0x9D, 0x00, 0x20,        // STA &2000,X
    0xE6, 0x72,              // INC &72
    0x68, 0xAA,              // PLA : TAX
    0x68,                    // done: PLA
    0x40,                    // RTI
};

uint8_t sector_byte(int track, int sector, int i) {
    return static_cast<uint8_t>(track * 10 + sector + i);
}

std::unique_ptr<Disc> make_disc() {
    std::vector<uint8_t> data(80 * 10 * 256);
    for (int t = 0; t < 80; ++t) {
        for (int s = 0; s < 10; ++s) {
            for (int i = 0; i < 256; ++i) {
                data[static_cast<size_t>((t * 10 + s) * 256 + i)] = sector_byte(t, s, i);
            }
        }
    }
    SsdFormatHandler handler;
    return std::move(handler.load(data, "/tmp/test8271.ssd").disc);
}

}  // namespace

TEST_CASE("A Model B reads a sector through the 8271 with an NMI per byte",
          "[disc][i8271][nmi][machine]") {
    ModelB machine;
    machine.memory().install_disc_controller(std::make_unique<Intel8271DiscController>(),
                                              "acorn-8271");
    machine.memory().disc_drive_0.insert(make_disc());

    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);
    mos[0x3FFC] = RESET_HANDLER & 0xFF;
    mos[0x3FFD] = RESET_HANDLER >> 8;
    mos[0x3FFA] = NMI_HANDLER & 0xFF;
    mos[0x3FFB] = NMI_HANDLER >> 8;
    machine.memory().load_mos(mos.data(), mos.size());
    for (size_t i = 0; i < reset_handler.size(); ++i) {
        machine.write(static_cast<uint16_t>(RESET_HANDLER + i), reset_handler[i]);
    }
    for (size_t i = 0; i < nmi_handler.size(); ++i) {
        machine.write(static_cast<uint16_t>(NMI_HANDLER + i), nmi_handler[i]);
    }
    machine.write(RESULT, 0xFF);

    M6502_Reset(&machine.cpu());
    machine.resume();
    machine.run(2'000'000);   // one second

    REQUIRE(machine.peek(RESULT) == 0x00);
    CHECK(machine.peek(0x0072) == 0x00);   // 256 bytes moved: the index wrapped
    for (int i = 0; i < 256; ++i) {
        CHECK(machine.peek(static_cast<uint16_t>(BUFFER + i)) == sector_byte(0, 3, i));
    }
}

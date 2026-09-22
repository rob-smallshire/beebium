// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <catch2/catch_test_macros.hpp>
#include <beebium/ModelBWatfordRomRamHardware.hpp>
#include <beebium/Machines.hpp>
#include <beebium/SlotTopology.hpp>
#include <array>

using namespace beebium;

namespace {

// Fill a 16K image with a constant byte, with a distinctive first byte.
std::array<uint8_t, 16384> make_image(uint8_t fill, uint8_t first) {
    std::array<uint8_t, 16384> img;
    img.fill(fill);
    img[0] = first;
    return img;
}

// Select the read bank via the &FE30 ROMSEL latch, the way the OS does.
void page_in(ModelBWatfordRomRamHardware& hw, uint8_t bank) {
    hw.write(0xFE30, bank);
}

// Strobe the &FF30 write-select latch to select `socket` for writes. The data
// written is a don't-care; the board latches the low nibble of the address.
void write_select(ModelBWatfordRomRamHardware& hw, uint8_t socket, uint8_t data = 0x00) {
    hw.write(static_cast<uint16_t>(0xFF30 + (socket & 0x0F)), data);
}

}  // namespace

TEST_CASE("Watford ROM/RAM defaults", "[hardware][watford]") {
    SECTION("machine identity") {
        REQUIRE(ModelBWatfordRomRamHardware::MACHINE_TYPE == "model-b-watford-rom-ram");
    }

    SECTION("BASIC defaults to slot 15 per the Watford manual") {
        REQUIRE(ModelBWatfordRomRamHardware::DEFAULT_LANGUAGE_SLOT == 15);

        ModelBWatfordRomRamHardware hw;
        auto basic = make_image(0xBA, 0xB0);
        hw.load_basic(basic.data(), basic.size());
        page_in(hw, 15);
        REQUIRE(hw.read(0x8000) == 0xB0);
        REQUIRE(hw.sideways.bank_type(15) == SlotType::Rom);
    }
}

TEST_CASE("Watford &FF30 write-select latch", "[hardware][watford][write_select]") {
    ModelBWatfordRomRamHardware hw;
    hw.configure_slot_as_ram(0);
    hw.configure_slot_as_ram(5);

    SECTION("writes go to the write-SELECTED socket, not the paged read bank") {
        page_in(hw, 0);            // bank 0 is paged in for reading
        write_select(hw, 0);
        hw.write(0x8000, 0x99);    // a sentinel in bank 0

        write_select(hw, 5);       // now writes are directed to socket 5
        REQUIRE(hw.write_select_socket() == 5);
        hw.write(0x8000, 0x5A);
        hw.write(0xBFFF, 0xA5);

        // Nothing from the socket-5 writes landed in the paged read bank 0.
        REQUIRE(hw.read(0x8000) == 0x99);

        // The data reads back only once socket 5 is paged in.
        page_in(hw, 5);
        REQUIRE(hw.read(0x8000) == 0x5A);
        REQUIRE(hw.read(0xBFFF) == 0xA5);
    }

    SECTION("the data written to the latch is irrelevant; only the address counts") {
        write_select(hw, 5, 0x00);
        REQUIRE(hw.write_select_socket() == 5);
        write_select(hw, 5, 0xFF);          // different data, same address
        REQUIRE(hw.write_select_socket() == 5);
    }

    SECTION("a four-byte !&FF30 write leaves socket 3 selected") {
        // !&FF30=0 writes &FF30,&FF31,&FF32,&FF33 in turn; the last wins.
        hw.write(0xFF30, 0x00);
        hw.write(0xFF31, 0x00);
        hw.write(0xFF32, 0x00);
        hw.write(0xFF33, 0x00);
        REQUIRE(hw.write_select_socket() == 3);
    }

    SECTION("reads of the sideways region stay on the &FE30 read-select") {
        auto rom = make_image(0x77, 0x71);
        hw.load_sideways_rom(9, rom.data(), rom.size());  // a motherboard ROM
        write_select(hw, 0);                              // writes -> socket 0
        page_in(hw, 9);                                   // reads  -> socket 9
        hw.write(0x8000, 0x42);                           // lands in bank 0
        REQUIRE(hw.read(0x8000) == 0x71);                 // still the ROM in bank 9
        page_in(hw, 0);
        REQUIRE(hw.read(0x8000) == 0x42);
    }
}

TEST_CASE("Watford ?&FF38 temporary write-protect trick", "[hardware][watford][write_select]") {
    ModelBWatfordRomRamHardware hw;
    hw.configure_slot_as_ram(0);
    // Socket 8 is a motherboard ROM socket, left empty here.

    write_select(hw, 0);
    hw.write(0x8000, 0xAA);         // lands in bank 0

    write_select(hw, 8);            // ?&FF38=0: select a non-RAM socket
    hw.write(0x8000, 0xBB);         // goes to empty socket 8 -> ignored

    page_in(hw, 0);
    REQUIRE(hw.read(0x8000) == 0xAA);   // bank 0 untouched
}

TEST_CASE("Watford S2 global write-protect", "[hardware][watford][s2]") {
    ModelBWatfordRomRamHardware hw;
    hw.configure_slot_as_ram(2);
    write_select(hw, 2);
    page_in(hw, 2);

    hw.write(0x8000, 0x10);
    REQUIRE(hw.read(0x8000) == 0x10);

    SECTION("engaged S2 blocks all sideways writes regardless of the latch") {
        hw.set_global_write_protect(true);
        REQUIRE(hw.global_write_protect());
        hw.write(0x8000, 0x20);                 // blocked
        REQUIRE(hw.read(0x8000) == 0x10);

        hw.set_global_write_protect(false);
        hw.write(0x8000, 0x30);
        REQUIRE(hw.read(0x8000) == 0x30);
    }
}

TEST_CASE("Watford S1 socket-14 read-protect", "[hardware][watford][s1]") {
    ModelBWatfordRomRamHardware hw;
    auto rom14 = make_image(0xE4, 0xE0);
    hw.load_sideways_rom(14, rom14.data(), rom14.size());
    auto rom15 = make_image(0xF5, 0xF0);
    hw.load_sideways_rom(15, rom15.data(), rom15.size());

    SECTION("engaged S1 makes socket 14 read back a constant; others unaffected") {
        page_in(hw, 14);
        REQUIRE(hw.read(0x8000) == 0xE0);

        hw.set_bank14_read_protect(true);
        REQUIRE(hw.bank14_read_protect());
        REQUIRE(hw.read(0x8000) == 0xFF);       // socket 14 has vanished

        page_in(hw, 15);                        // another socket reads normally
        REQUIRE(hw.read(0x8000) == 0xF0);

        page_in(hw, 14);
        hw.set_bank14_read_protect(false);
        REQUIRE(hw.read(0x8000) == 0xE0);       // back to normal
    }
}

TEST_CASE("Watford reset behaviour", "[hardware][watford][reset]") {
    ModelBWatfordRomRamHardware hw;
    write_select(hw, 7);
    hw.set_global_write_protect(true);
    hw.set_bank14_read_protect(true);

    hw.reset();

    SECTION("hard reset zeroes the write-select latch") {
        REQUIRE(hw.write_select_socket() == 0);
    }
    SECTION("the S1/S2 switches persist across reset") {
        REQUIRE(hw.global_write_protect());
        REQUIRE(hw.bank14_read_protect());
    }
}

TEST_CASE("Watford topology", "[hardware][watford][topology]") {
    auto topo = ModelBWatfordRomRamHardware::slot_topology();

    SECTION("16 independent, non-runtime-configurable slots, no per-slot write-protect") {
        REQUIRE(topo.sockets.size() == 16);
        REQUIRE_FALSE(topo.has_aliasing);
        for (const auto& s : topo.sockets) {
            REQUIRE(s.slots.size() == 1);
            REQUIRE_FALSE(s.runtime_configurable);
            REQUIRE_FALSE(s.supports_write_protect);
        }
    }

    SECTION("bank map: 0-7 RAM, 8-13 & 15 ROM, 14 ROM+RAM") {
        for (const auto& s : topo.sockets) {
            int slot = s.slots[0];
            if (slot <= 7) {
                REQUIRE(s.supports_ram);
                REQUIRE_FALSE(s.supports_rom);
            } else if (slot == 14) {
                REQUIRE(s.supports_ram);
                REQUIRE(s.supports_rom);
            } else {  // 8-13, 15
                REQUIRE_FALSE(s.supports_ram);
                REQUIRE(s.supports_rom);
            }
        }
    }
}

TEST_CASE("ModelBWatfordRomRam Machine instantiates and reads banks",
          "[machine][watford][peek_region]") {
    ModelBWatfordRomRam machine;
    auto& memory = machine.state().memory;

    auto rom = make_image(0x42, 0xAA);
    memory.load_sideways_rom(12, rom.data(), rom.size());

    REQUIRE(machine.memory().peek_region("bank_12", 0x8000) == 0xAA);
}

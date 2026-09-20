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
#include <beebium/ModelBAtplSidewiseHardware.hpp>
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

// Select sideways bank via the &FE30 ROMSEL latch, the way the OS does.
void page_in(ModelBAtplSidewiseHardware& hw, uint8_t bank) {
    hw.write(0xFE30, bank);
}

}  // namespace

TEST_CASE("ATPL Sidewise defaults", "[hardware][atpl_sidewise]") {
    SECTION("machine identity") {
        REQUIRE(ModelBAtplSidewiseHardware::MACHINE_TYPE == "model-b-atpl-sidewise");
    }

    SECTION("BASIC defaults to slot 14 per the Sidewise manual") {
        REQUIRE(ModelBAtplSidewiseHardware::DEFAULT_LANGUAGE_SLOT == 14);

        ModelBAtplSidewiseHardware hw;
        auto basic = make_image(0xBA, 0xB0);
        hw.load_basic(basic.data(), basic.size());
        page_in(hw, 14);
        REQUIRE(hw.read(0x8000) == 0xB0);
        REQUIRE(hw.sideways.bank_type(14) == SlotType::Rom);
    }
}

TEST_CASE("ATPL Sidewise slot-15 write-through", "[hardware][atpl_sidewise][write_through]") {
    ModelBAtplSidewiseHardware hw;
    hw.configure_slot_as_ram(15);

    // A ROM sits in slot 14 (as BASIC would).
    auto rom14 = make_image(0x14, 0xE4);
    hw.load_sideways_rom(14, rom14.data(), rom14.size());

    SECTION("write reaches slot-15 RAM regardless of the paged ROM") {
        page_in(hw, 14);               // slot 14 (ROM) is paged in
        hw.write(0x8000, 0x5A);        // write-through to slot-15 RAM
        hw.write(0xBFFF, 0xA5);

        // Reads are ROMSEL-gated: still see the paged ROM, unaffected.
        REQUIRE(hw.read(0x8000) == 0xE4);

        // The RAM reads back only once slot 15 is paged in.
        page_in(hw, 15);
        REQUIRE(hw.read(0x8000) == 0x5A);
        REQUIRE(hw.read(0xBFFF) == 0xA5);
    }

    SECTION("write-through works across the whole &8000-&BFFF region") {
        page_in(hw, 0);                // an empty slot is paged in
        hw.write(0x9FFF, 0x11);        // would be 15a on the real board
        hw.write(0xA000, 0x22);        // would be 15b on the real board
        page_in(hw, 15);
        REQUIRE(hw.read(0x9FFF) == 0x11);
        REQUIRE(hw.read(0xA000) == 0x22);
    }
}

TEST_CASE("ATPL Sidewise slot-15 write-protect", "[hardware][atpl_sidewise][write_protect]") {
    ModelBAtplSidewiseHardware hw;
    hw.configure_slot_as_ram(15);
    auto rom14 = make_image(0x14, 0xE4);
    hw.load_sideways_rom(14, rom14.data(), rom14.size());

    SECTION("protected RAM ignores writes and retains contents") {
        page_in(hw, 15);
        hw.write(0x8000, 0x11);
        REQUIRE(hw.read(0x8000) == 0x11);

        hw.set_slot_write_protected(15, true);
        REQUIRE(hw.is_slot_write_protected(15));
        hw.write(0x8000, 0x22);                 // swallowed
        REQUIRE(hw.read(0x8000) == 0x11);

        hw.set_slot_write_protected(15, false);
        hw.write(0x8000, 0x33);
        REQUIRE(hw.read(0x8000) == 0x33);
    }

    SECTION("write-protect also blocks write-through from another paged bank") {
        page_in(hw, 15);
        hw.write(0x8000, 0xAB);
        hw.set_slot_write_protected(15, true);

        page_in(hw, 14);                        // ROM paged in
        hw.write(0x8000, 0xCD);                 // swallowed by write-protect

        page_in(hw, 15);
        REQUIRE(hw.read(0x8000) == 0xAB);
    }
}

TEST_CASE("ATPL Sidewise slot 15 fitted as ROM", "[hardware][atpl_sidewise]") {
    ModelBAtplSidewiseHardware hw;
    auto rom15 = make_image(0xCC, 0xC5);
    hw.load_sideways_rom(15, rom15.data(), rom15.size());  // slot 15 is ROM, not RAM

    SECTION("no write-through; the ROM ignores writes") {
        page_in(hw, 15);
        hw.write(0x8000, 0x42);
        REQUIRE(hw.read(0x8000) == 0xC5);   // unchanged ROM content
    }
}

TEST_CASE("ATPL Sidewise topology", "[hardware][atpl_sidewise][topology]") {
    auto topo = ModelBAtplSidewiseHardware::slot_topology();

    SECTION("16 independent, non-runtime-configurable slots") {
        REQUIRE(topo.sockets.size() == 16);
        REQUIRE_FALSE(topo.has_aliasing);
        for (const auto& s : topo.sockets) {
            REQUIRE(s.slots.size() == 1);
            REQUIRE_FALSE(s.runtime_configurable);
        }
    }

    SECTION("only slot 15 supports RAM") {
        for (const auto& s : topo.sockets) {
            REQUIRE(s.supports_rom);
            if (s.slots[0] == 15) {
                REQUIRE(s.supports_ram);
            } else {
                REQUIRE_FALSE(s.supports_ram);
            }
        }
    }
}

TEST_CASE("ModelBAtplSidewise Machine instantiates and reads banks",
          "[machine][atpl_sidewise][peek_region]") {
    ModelBAtplSidewise machine;
    auto& memory = machine.state().memory;

    auto rom = make_image(0x42, 0xAA);
    memory.load_sideways_rom(15, rom.data(), rom.size());

    REQUIRE(machine.memory().peek_region("bank_15", 0x8000) == 0xAA);
}

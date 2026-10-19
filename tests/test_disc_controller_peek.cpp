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

// A debugger peek of the disc controller reads its registers without the side
// effects a CPU read has: reading the WD1770's status clears INTRQ and reading
// its data register clears DRQ, so a peek that took the CPU's path would change
// the machine it inspects (#200).

#include <catch2/catch_test_macros.hpp>

#include "beebium/MemoryMap.hpp"
#include "beebium/ModelBHardware.hpp"
#include "beebium/ModelBPlusHardware.hpp"
#include "beebium/disc/Acorn1770DiscController.hpp"
#include "beebium/disc/WD1770.hpp"

#include <memory>

using namespace beebium;

namespace {

constexpr uint8_t FORCE_INTERRUPT_IMMEDIATE = 0xD8;  // raises INTRQ at once

// A device whose read has a side effect and whose peek has none.
struct CountingDevice {
    int reads = 0;
    uint8_t read(uint16_t) { ++reads; return 0x11; }
    void write(uint16_t, uint8_t) {}
    uint8_t peek(uint16_t) const { return 0x22; }
};

// A device with no peek: a peek falls back to its read.
struct PlainDevice {
    uint8_t read(uint16_t) { return 0x33; }
    void write(uint16_t, uint8_t) {}
};

}  // namespace

TEST_CASE("MemoryMap peek uses a device's peek and leaves the bus alone", "[memory][peek]") {
    CountingDevice counting;
    PlainDevice plain;
    MemoryMap map{make_region<0x0000, 0x00FF>(counting), make_region<0x0100, 0x01FF>(plain)};

    CHECK(map.peek(0x0010) == 0x22);
    CHECK(counting.reads == 0);
    CHECK(map.peek(0x0110) == 0x33);
    CHECK(map.last_bus_value() == 0xFF);
}

TEST_CASE("WD1770 peek leaves INTRQ and DRQ as they were", "[disc][wd1770][peek]") {
    WD1770 fdc;
    fdc.write(0, FORCE_INTERRUPT_IMMEDIATE);
    REQUIRE(fdc.intrq());

    CHECK(fdc.peek(0) == fdc.peek(0));
    CHECK(fdc.intrq());
    fdc.peek(3);
    CHECK(fdc.intrq());

    fdc.write(1, 0x5A);
    CHECK(fdc.peek(1) == 0x5A);
}

TEST_CASE("Acorn 1770 board peek leaves INTRQ as it was", "[disc][acorn1770][peek]") {
    Acorn1770DiscController controller;
    controller.write(0x04, FORCE_INTERRUPT_IMMEDIATE);
    REQUIRE(controller.wd1770().intrq());

    controller.peek(0x04);
    CHECK(controller.wd1770().intrq());
    CHECK(controller.peek(0x00) == 0xFF);
}

TEST_CASE("Model B peek of &FE84 leaves the 1770's INTRQ as it was", "[disc][acorn1770][peek]") {
    ModelBHardware hw;
    auto controller = std::make_unique<Acorn1770DiscController>();
    auto& wd1770 = controller->wd1770();
    hw.disc_socket.install(std::move(controller));
    hw.write(0xFE84, FORCE_INTERRUPT_IMMEDIATE);
    REQUIRE(wd1770.intrq());

    hw.peek(0xFE84);
    hw.peek(0xFE87);
    CHECK(wd1770.intrq());
}

TEST_CASE("Model B+ peek of &FE84 leaves the 1770's INTRQ as it was", "[disc][wd1770][peek]") {
    ModelBPlusHardware hw;
    hw.write(0xFE84, FORCE_INTERRUPT_IMMEDIATE);
    REQUIRE(hw.disc_controller.intrq());

    hw.peek(0xFE84);
    hw.peek(0xFE87);
    CHECK(hw.disc_controller.intrq());
}

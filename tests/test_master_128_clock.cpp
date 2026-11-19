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

// test_master_128_clock.cpp
// The Master 128's MC146818 on the System VIA (MasterSystemViaPeripheral),
// driven through the VIA's registers with the sequence the New Advanced User
// Guide gives (19.6, its "cmosaccess" routine), and the clock's seed and
// battery-backed unit.

#include <beebium/Machines.hpp>
#include <beebium/Master128CmosSeed.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>

using namespace beebium;

namespace {

using HW = Master128Hardware;

constexpr uint16_t REGB = 0xFE40;     // System VIA port B
constexpr uint16_t DDRB = 0xFE42;
constexpr uint16_t DDRA = 0xFE43;
constexpr uint16_t SLOWBUS = 0xFE4F;  // System VIA port A, no handshake

// The guide's routine, register by register. The MOS sets DDRB to make PB6
// and PB7 outputs.
struct Cmos {
    HW& hw;

    explicit Cmos(HW& h) : hw(h) { hw.write(DDRB, 0xCF); }

    void address(uint8_t reg) {
        hw.write(REGB, 0x02);  // CE and DS inactive
        hw.write(REGB, 0x82);  // AS active
        hw.write(DDRA, 0xFF);  // port A an output
        hw.write(SLOWBUS, reg);
        hw.write(REGB, 0xC2);  // CE active
        hw.write(REGB, 0x42);  // AS low: the address latches
    }

    void finish() {
        hw.write(REGB, 0x42);  // DS inactive
        hw.write(REGB, 0x02);  // CE inactive
    }

    uint8_t read(uint8_t reg) {
        address(reg);
        hw.write(REGB, 0x49);  // R/W high: a read
        hw.write(DDRA, 0x00);  // port A an input
        hw.write(REGB, 0x4A);  // DS active
        const uint8_t value = hw.read(SLOWBUS);
        finish();
        return value;
    }

    void write(uint8_t reg, uint8_t value) {
        address(reg);
        hw.write(REGB, 0x41);  // R/W low: a write
        hw.write(REGB, 0x4A);  // DS active
        hw.write(SLOWBUS, value);
        finish();
    }
};

}  // namespace

TEST_CASE("Master 128 clock: a CMOS byte written through the System VIA reads back",
          "[master128][clock][via]") {
    auto hw = std::make_unique<HW>();
    Cmos cmos(*hw);
    cmos.write(0x0E + 46, 0x5C);  // CMOS byte 46, the first user byte
    CHECK(hw->rtc.peek_register(0x0E + 46) == 0x5C);
    CHECK(cmos.read(0x0E + 46) == 0x5C);
    cmos.write(0x0E + 49, 0xA3);
    CHECK(cmos.read(0x0E + 49) == 0xA3);
    CHECK(cmos.read(0x0E + 46) == 0x5C);
}

TEST_CASE("Master 128 clock: the seed's registers read through the System VIA",
          "[master128][clock][via]") {
    auto hw = std::make_unique<HW>();
    Cmos cmos(*hw);
    CHECK(cmos.read(Mc146818Rtc::REG_A) == 0x20);
    CHECK(cmos.read(Mc146818Rtc::REG_B) == 0x02);
    CHECK(cmos.read(0x0E + 5) == 0xCD);  // LANG 12, FILE 13
}

TEST_CASE("Master 128 clock: the address latches only with CE high when AS falls",
          "[master128][clock][via]") {
    auto hw = std::make_unique<HW>();
    Cmos cmos(*hw);
    cmos.read(0x0E + 10);  // the clock's address is now CMOS byte 10
    REQUIRE(hw->rtc.address() == 0x0E + 10);
    // AS falls with CE low: the address stays.
    hw->write(REGB, 0x02);
    hw->write(REGB, 0x82);
    hw->write(DDRA, 0xFF);
    hw->write(SLOWBUS, 0x0E + 20);
    hw->write(REGB, 0x02);
    CHECK(hw->rtc.address() == 0x0E + 10);
}

TEST_CASE("Master 128 clock: a write latches the bus when DS falls, not before",
          "[master128][clock][via]") {
    auto hw = std::make_unique<HW>();
    Cmos cmos(*hw);
    cmos.address(0x0E + 47);
    hw->write(REGB, 0x41);
    hw->write(REGB, 0x4A);
    hw->write(SLOWBUS, 0x3C);
    CHECK(hw->rtc.peek_register(0x0E + 47) == 0x00);
    hw->write(REGB, 0x42);  // DS falls
    CHECK(hw->rtc.peek_register(0x0E + 47) == 0x3C);
    hw->write(REGB, 0x02);
}

TEST_CASE("Master 128 clock: the clock drives port A only while a read's DS is high",
          "[master128][clock][via]") {
    auto hw = std::make_unique<HW>();
    Cmos cmos(*hw);
    cmos.read(Mc146818Rtc::REG_A);
    CHECK_FALSE(hw->system_via_peripheral.clock_driving());
    cmos.address(Mc146818Rtc::REG_A);
    hw->write(REGB, 0x49);
    hw->write(DDRA, 0x00);
    CHECK_FALSE(hw->system_via_peripheral.clock_driving());
    hw->write(REGB, 0x4A);
    CHECK(hw->system_via_peripheral.clock_driving());
    cmos.finish();
    CHECK_FALSE(hw->system_via_peripheral.clock_driving());
}

TEST_CASE("Master 128 clock: Break reaches the chip's RESET pin", "[master128][clock][reset]") {
    auto hw = std::make_unique<HW>();
    Cmos cmos(*hw);
    // PIE, AIE and UIE set: RESET clears them and keeps the rest.
    cmos.write(Mc146818Rtc::REG_B, 0x72);
    REQUIRE(hw->rtc.peek_register(Mc146818Rtc::REG_B) == 0x72);
    cmos.write(0x0E + 48, 0x99);
    hw->soft_reset();
    CHECK(hw->rtc.peek_register(Mc146818Rtc::REG_B) == 0x02);
    CHECK(hw->rtc.peek_register(0x0E + 48) == 0x99);
}

TEST_CASE("Master 128 CMOS seed: the Welcome Guide configuration",
          "[master128][clock][seed]") {
    using namespace master_128_cmos_seed;
    const auto cmos = welcome_guide();
    CHECK(cmos[0] == 0x00);            // station 0
    CHECK(cmos[5] == 0xCD);            // LANG 12, FILE 13
    CHECK(cmos[6] == 0xFF);            // every ROM inserted
    CHECK(cmos[7] == 0xFF);
    CHECK(cmos[10] == 0x17);           // TV 0,1; MODE 7
    CHECK(cmos[11] == 0xE3);           // FLOPPY, NODIR, CAPS, FDRIVE 3
    CHECK(cmos[12] == 50);             // DELAY 50
    CHECK(cmos[13] == 10);             // REPEAT 10
    CHECK(cmos[14] == 10);             // IGNORE 10
    CHECK(cmos[15] == 0x2D);           // PRINT 1, BAUD 4, TUBE
    CHECK(cmos[16] == 0x82);           // DATA 4, LOUD; INTUBE, SCROLL, NOBOOT
    CHECK(welcome_guide(42)[0] == 42);
}

TEST_CASE("Master 128 CMOS is one battery-backed unit, master-128-cmos",
          "[master128][clock][battery]") {
    auto machine = std::make_unique<Master128>();
    auto units = machine->memory().battery_units();
    REQUIRE(units.size() == 1);
    CHECK(units[0]->name() == "master-128-cmos");
    CHECK(units[0]->hardware_descriptor() == "Master 128: MC146818");
    CHECK(units[0]->layout_version() == 1);
    // A fresh machine holds the seed.
    CHECK(machine->memory().rtc.peek_register(0x0E + 5) == 0xCD);
    // Its contents round-trip into another machine.
    machine->memory().rtc.write_address(0x0E + 46);
    machine->memory().rtc.write_data(0x77);
    const auto contents = units[0]->serialise();
    auto other = std::make_unique<Master128>();
    REQUIRE(other->memory().battery_units()[0]->restore(contents));
    CHECK(other->memory().rtc.peek_register(0x0E + 46) == 0x77);
}

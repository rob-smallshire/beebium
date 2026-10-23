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
#include <beebium/extension/UserPort.hpp>
#include <beebium/UserViaPeripheral.hpp>
#include <beebium/Via6522.hpp>
#include <beebium/ModelBHardware.hpp>
#include <beebium/ModelBPlusHardware.hpp>
#include <beebium/ModelBRomRamBoardHardware.hpp>

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// Mock UserPortDevice for testing
//////////////////////////////////////////////////////////////////////////////

class MockUserPortDevice : public UserPortDevice {
public:
    uint8_t update_port_b(uint8_t output, uint8_t ddr) override {
        last_output = output;
        last_ddr = ddr;
        update_port_b_call_count++;
        return input_value;
    }

    void update_control_lines(uint8_t& cb1, uint8_t& cb2) override {
        update_control_lines_call_count++;
        last_cb2_from_via = cb2;
        cb1 = cb1_drive;
        cb2 = cb2_drive;
    }

    // Values the mock returns to the VIA
    uint8_t input_value = 0xFF;
    uint8_t cb1_drive = 1;   // default: not asserted
    uint8_t cb2_drive = 1;

    // Values captured from the VIA
    uint8_t last_output = 0;
    uint8_t last_ddr = 0;
    uint8_t last_cb2_from_via = 0;
    int update_port_b_call_count = 0;
    int update_control_lines_call_count = 0;
};

//////////////////////////////////////////////////////////////////////////////
// UserPort attachment tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("UserPort attachment", "[user-port]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    UserPort port(peripheral);

    SECTION("Initially unoccupied") {
        REQUIRE_FALSE(port.is_occupied());
    }

    SECTION("Attach succeeds") {
        MockUserPortDevice device;
        port.attach(device);
        REQUIRE(port.is_occupied());
    }

    SECTION("Second attach throws") {
        MockUserPortDevice device1;
        MockUserPortDevice device2;
        port.attach(device1);
        REQUIRE_THROWS_AS(port.attach(device2), std::runtime_error);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Port B forwarding tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("UserPort forwards Port B writes to device", "[user-port][port-b]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    UserPort port(peripheral);
    MockUserPortDevice device;
    port.attach(device);

    SECTION("Device receives output and DDR on ORB write") {
        // Set DDR: PB0-PB2 as outputs
        via.write(Via6522::REG_DDRB, 0x07);

        // Write to ORB
        via.write(Via6522::REG_ORB, 0x05);

        REQUIRE(device.update_port_b_call_count > 0);
        REQUIRE(device.last_output == 0x05);
        REQUIRE(device.last_ddr == 0x07);
    }

    SECTION("Device receives DDR changes") {
        via.write(Via6522::REG_DDRB, 0xA7);
        REQUIRE(device.last_ddr == 0xA7);
    }

    SECTION("Device input pins are read by VIA") {
        // All pins as inputs
        via.write(Via6522::REG_DDRB, 0x00);

        // Device drives PB0 low, rest high
        device.input_value = 0xFE;

        // Tick VIA to update port pins
        via.update_phi2_trailing_edge();

        // Read IRB should see device's input
        uint8_t read = via.read(Via6522::REG_ORB);
        REQUIRE((read & 0x01) == 0x00);  // PB0 low from device
    }
}

//////////////////////////////////////////////////////////////////////////////
// CB1/CB2 forwarding tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("UserPort forwards CB1/CB2 to device", "[user-port][control-lines]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    UserPort port(peripheral);
    MockUserPortDevice device;
    port.attach(device);

    SECTION("Device can drive CB1 low to trigger interrupt") {
        // Enable CB1 interrupt (positive edge)
        via.write(Via6522::REG_IER, 0x90);  // Set bit 4 (CB1)
        // PCR bit 4: positive edge on CB1
        via.write(Via6522::REG_PCR, 0x10);

        // Device drives CB1 low then high (positive edge)
        device.cb1_drive = 0;
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();

        device.cb1_drive = 1;
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();

        // CB1 interrupt flag should be set
        REQUIRE((via.read(Via6522::REG_IFR) & 0x10) == 0x10);
    }

    SECTION("Control lines callback is invoked") {
        via.update_phi2_trailing_edge();
        REQUIRE(device.update_control_lines_call_count > 0);
    }
}

//////////////////////////////////////////////////////////////////////////////
// UserViaPeripheral composite tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("UserViaPeripheral forwards Port B and CB1/CB2 only", "[user-port][composite]") {
    UserViaPeripheral peripheral;

    SECTION("Empty composite behaves as the ViaPeripheral defaults") {
        REQUIRE_FALSE(peripheral.has_user_port_device());
        REQUIRE(peripheral.update_port_a(0x00, 0xFF) == 0xFF);
        REQUIRE(peripheral.update_port_b(0x00, 0xFF) == 0xFF);
        uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(ca1 == 1);
        REQUIRE(ca2 == 1);
        REQUIRE(cb1 == 1);
        REQUIRE(cb2 == 1);
    }

    SECTION("Attached device sees Port B and CB1/CB2; Port A and CA1/CA2 stay default") {
        MockUserPortDevice device;
        device.input_value = 0x5A;
        device.cb1_drive = 0;
        device.cb2_drive = 0;
        peripheral.attach_user_port_device(device);
        REQUIRE(peripheral.has_user_port_device());

        REQUIRE(peripheral.update_port_b(0x12, 0x34) == 0x5A);
        REQUIRE(device.last_output == 0x12);
        REQUIRE(device.last_ddr == 0x34);

        // Port A is not the device's: inputs read high regardless of the device
        REQUIRE(peripheral.update_port_a(0x00, 0x00) == 0xFF);

        uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(ca1 == 1);
        REQUIRE(ca2 == 1);
        REQUIRE(cb1 == 0);
        REQUIRE(cb2 == 0);
        REQUIRE(device.update_control_lines_call_count == 1);
    }

    SECTION("Second attach throws") {
        MockUserPortDevice device1;
        MockUserPortDevice device2;
        peripheral.attach_user_port_device(device1);
        REQUIRE_THROWS_AS(peripheral.attach_user_port_device(device2), std::runtime_error);
    }
}

TEST_CASE("UserPort handle attaches through a UserViaPeripheral on a Via6522", "[user-port][composite]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    REQUIRE(via.peripheral() == &peripheral);

    UserPort port(peripheral);
    MockUserPortDevice device;
    port.attach(device);

    // The handle reports through the composite...
    REQUIRE(port.is_occupied());
    REQUIRE(peripheral.has_user_port_device());

    // ...and the VIA reaches the device through the composite it already holds
    via.write(Via6522::REG_DDRB, 0x00);
    device.input_value = 0x7F;
    via.update_phi2_trailing_edge();
    REQUIRE(via.read(Via6522::REG_ORB) == 0x7F);
    REQUIRE(via.peripheral() == &peripheral);
}

//////////////////////////////////////////////////////////////////////////////
// Port A isolation tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("UserPort does not affect Port A", "[user-port][isolation]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    UserPort port(peripheral);
    MockUserPortDevice device;
    device.input_value = 0x00;  // Try to drive everything low
    port.attach(device);

    SECTION("Port A inputs remain high (default 0xFF)") {
        via.write(Via6522::REG_DDRA, 0x00);  // All inputs
        via.update_phi2_trailing_edge();
        uint8_t read = via.read(Via6522::REG_ORA_NH);
        REQUIRE(read == 0xFF);
    }
}

//////////////////////////////////////////////////////////////////////////////
// HasUserPort concept tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("HasUserPort concept matches hardware models", "[user-port][concept]") {
    STATIC_REQUIRE(HasUserPort<ModelBHardware>);
    STATIC_REQUIRE(HasUserPort<ModelBPlusHardware>);
    STATIC_REQUIRE(HasUserPort<ModelBRomRamBoardHardware>);
}

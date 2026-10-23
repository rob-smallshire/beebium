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
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>
#include <beebium/UserViaPeripheral.hpp>
#include <beebium/Via6522.hpp>
#include <beebium/ModelBHardware.hpp>
#include <beebium/ModelBPlusHardware.hpp>
#include <beebium/ModelBRomRamBoardHardware.hpp>

#include <stdexcept>
#include <string>
#include <vector>

using namespace beebium;

//////////////////////////////////////////////////////////////////////////////
// Fake PrinterPortDevice: records what it is told, drives /ACK as scripted
//////////////////////////////////////////////////////////////////////////////

class FakePrinterPortDevice : public PrinterPortDevice {
public:
    void update_data_lines(uint8_t data) override {
        data_line_updates.push_back(data);
    }

    uint8_t update_control_lines(uint8_t strobe, uint8_t data) override {
        ++control_line_call_count;
        last_strobe = strobe;
        last_data = data;
        if (strobe_level == 1 && strobe == 0) {
            latched_data = data;
            ++strobe_count;
            ticks_until_ack = ack_delay_ticks;
            ack_ticks_remaining = 0;
        }
        strobe_level = strobe;
        if (ack_delay_ticks >= 0 && strobe_count > 0) {
            if (ticks_until_ack > 0) {
                --ticks_until_ack;
                if (ticks_until_ack == 0) {
                    ack_ticks_remaining = ack_pulse_ticks;
                }
            }
        }
        if (ack_ticks_remaining > 0) {
            --ack_ticks_remaining;
            return 0;
        }
        return ack_drive;
    }

    // Scripted /ACK: a fixed level, or a pulse so many ticks after a strobe
    uint8_t ack_drive = 1;
    int ack_delay_ticks = -1;   // -1: never pulse, just drive ack_drive
    int ack_pulse_ticks = 3;

    // Captured
    std::vector<uint8_t> data_line_updates;
    uint8_t last_strobe = 1;
    uint8_t last_data = 0;
    uint8_t latched_data = 0;
    int strobe_count = 0;
    int control_line_call_count = 0;

private:
    uint8_t strobe_level = 1;
    int ticks_until_ack = 0;
    int ack_ticks_remaining = 0;
};

//////////////////////////////////////////////////////////////////////////////
// Attachment
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("PrinterPort attachment", "[printer-port]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    PrinterPort port(peripheral);

    SECTION("Initially unoccupied") {
        REQUIRE_FALSE(port.is_occupied());
        REQUIRE_FALSE(peripheral.has_printer_port_device());
    }

    SECTION("Attach a device") {
        FakePrinterPortDevice device;
        port.attach(device);
        REQUIRE(port.is_occupied());
        REQUIRE(peripheral.has_printer_port_device());
    }

    SECTION("Second attach throws") {
        FakePrinterPortDevice device1;
        FakePrinterPortDevice device2;
        port.attach(device1);
        REQUIRE_THROWS_AS(port.attach(device2), std::runtime_error);
        try {
            port.attach(device2);
            FAIL("expected std::runtime_error");
        } catch (const std::runtime_error& e) {
            REQUIRE(std::string(e.what()) ==
                "PrinterPort: a device is already attached (only one printer port device is supported)");
        }
    }

    SECTION("Printer port and User Port halves are independent") {
        FakePrinterPortDevice device;
        port.attach(device);
        REQUIRE(peripheral.has_printer_port_device());
        REQUIRE_FALSE(peripheral.has_user_port_device());
    }
}

//////////////////////////////////////////////////////////////////////////////
// Data lines
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Printer port data lines reach the device with input bits high", "[printer-port][data]") {
    UserViaPeripheral peripheral;
    FakePrinterPortDevice device;
    peripheral.attach_printer_port_device(device);

    // Attaching presents the current lines: all input at reset, floating high
    REQUIRE(device.data_line_updates.size() == 1);
    REQUIRE(device.data_line_updates.back() == 0xFF);

    SECTION("All outputs: the data is the output register") {
        REQUIRE(peripheral.update_port_a(0x41, 0xFF) == 0xFF);  // buffer: VIA inputs read high
        REQUIRE(device.data_line_updates.size() == 2);
        REQUIRE(device.data_line_updates.back() == 0x41);
    }

    SECTION("Input bits float high through the 74LS244") {
        peripheral.update_port_a(0x41, 0x0F);
        REQUIRE(device.data_line_updates.back() == 0xF1);
    }

    SECTION("Only a change in the lines is reported") {
        peripheral.update_port_a(0x41, 0xFF);
        peripheral.update_port_a(0x41, 0xFF);
        peripheral.update_port_a(0x41, 0xFF);
        REQUIRE(device.data_line_updates.size() == 2);
        peripheral.update_port_a(0x42, 0xFF);
        REQUIRE(device.data_line_updates.size() == 3);
        REQUIRE(device.data_line_updates.back() == 0x42);
    }

    SECTION("Port A inputs always read high regardless of the device") {
        REQUIRE(peripheral.update_port_a(0x00, 0x00) == 0xFF);
    }
}

//////////////////////////////////////////////////////////////////////////////
// Control lines
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Printer port passes strobe and data each tick and places /ACK on CA1", "[printer-port][control-lines]") {
    UserViaPeripheral peripheral;
    FakePrinterPortDevice device;
    peripheral.attach_printer_port_device(device);
    peripheral.update_port_a(0x5A, 0xFF);

    SECTION("Strobe level and data reach the device on every tick") {
        uint8_t ca1 = 1, ca2 = 0, cb1 = 1, cb2 = 1;
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(device.control_line_call_count == 2);
        REQUIRE(device.last_strobe == 0);
        REQUIRE(device.last_data == 0x5A);
    }

    SECTION("The returned level lands on CA1; CA2 and port B are untouched") {
        device.ack_drive = 0;
        uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(ca1 == 0);
        REQUIRE(ca2 == 1);
        REQUIRE(cb1 == 1);
        REQUIRE(cb2 == 1);

        device.ack_drive = 1;
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(ca1 == 1);
    }

    SECTION("Without a device CA1 is left as the VIA set it") {
        UserViaPeripheral empty;
        uint8_t ca1 = 1, ca2 = 0, cb1 = 1, cb2 = 1;
        empty.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(ca1 == 1);
        ca1 = 0;
        empty.update_control_lines(ca1, ca2, cb1, cb2);
        REQUIRE(ca1 == 0);
    }
}

TEST_CASE("A strobe through a real Via6522 reaches the device and its /ACK raises IFR CA1", "[printer-port][via]") {
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    PrinterPort port(peripheral);
    FakePrinterPortDevice device;
    device.ack_delay_ticks = 2;
    device.ack_pulse_ticks = 3;
    port.attach(device);

    auto tick = [&] {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    };

    via.write(Via6522::REG_DDRA, 0xFF);   // all data lines output
    via.write(Via6522::REG_PCR, 0x0A);    // CA1 negative edge, CA2 pulse output
    tick();
    tick();
    REQUIRE((via.read(Via6522::REG_IFR) & 0x02) == 0);
    REQUIRE(device.strobe_count == 0);

    // Writing ORA pulses /STROBE low; the device latches the data at that edge
    via.write(Via6522::REG_ORA, 0x41);
    REQUIRE(device.strobe_count == 1);
    REQUIRE(device.latched_data == 0x41);
    REQUIRE((via.read(Via6522::REG_IFR) & 0x02) == 0);

    // The device changes the data lines later; the latched byte is unaffected
    bool ca1_flag_seen = false;
    for (int i = 0; i < 10 && !ca1_flag_seen; ++i) {
        tick();
        ca1_flag_seen = (via.read(Via6522::REG_IFR) & 0x02) != 0;
    }
    REQUIRE(ca1_flag_seen);
    REQUIRE(device.latched_data == 0x41);

    // Once the VIA's two-tick pulse has ended, /STROBE is back high at the device
    tick();
    tick();
    REQUIRE(device.last_strobe == 1);

    // Writing IFR clears the CA1 flag; a read of ORA would clear it too, but
    // in pulse mode that read is itself a strobe, which is why the MOS and
    // this test clear the flag through IFR
    via.write(Via6522::REG_IFR, 0x02);
    REQUIRE((via.read(Via6522::REG_IFR) & 0x02) == 0);

    // A second byte strobes again
    via.write(Via6522::REG_ORA, 0x42);
    REQUIRE(device.strobe_count == 2);
    REQUIRE(device.latched_data == 0x42);
}

//////////////////////////////////////////////////////////////////////////////
// Hardware policies
//////////////////////////////////////////////////////////////////////////////

static_assert(HasPrinterPort<ModelBHardware>);
static_assert(HasPrinterPort<ModelBPlusHardware>);
static_assert(HasPrinterPort<ModelBRomRamBoardHardware>);

TEST_CASE("Hardware policies expose a printer port on the User VIA composite", "[printer-port][hardware]") {
    ModelBHardware hw;
    REQUIRE_FALSE(hw.printer_port().is_occupied());
    FakePrinterPortDevice device;
    hw.printer_port().attach(device);
    REQUIRE(hw.printer_port().is_occupied());
    REQUIRE(hw.user_via_peripheral.has_printer_port_device());
    REQUIRE(hw.user_via.peripheral() == &hw.user_via_peripheral);
}

//////////////////////////////////////////////////////////////////////////////
// ExtensionContext
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("ExtensionContext hands out the printer port", "[printer-port][extension-context]") {
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);

    SECTION("Available when the machine supplies one") {
        ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
        REQUIRE(ctx.has<PrinterPort>());
        REQUIRE(&ctx.get<PrinterPort>() == &port);
    }

    SECTION("Absent otherwise, with a message naming the point") {
        ExtensionContext ctx;
        REQUIRE_FALSE(ctx.has<PrinterPort>());
        try {
            ctx.get<PrinterPort>();
            FAIL("expected std::runtime_error");
        } catch (const std::runtime_error& e) {
            REQUIRE(std::string(e.what()) ==
                "ExtensionContext: printer-port not available on this machine");
        }
    }
}

TEST_CASE("A strobe from the MOS's power-on PCR state latches the byte written to ORA", "[printer-port][via]") {
    // The MOS leaves the User VIA with PCR = &0E (CA2 output high) and DDRA =
    // &FF. Software then selects pulse mode and writes ORA; the device must
    // latch that byte, not the lines as they stood before the write.
    Via6522 via;
    UserViaPeripheral peripheral;
    via.set_peripheral(&peripheral);
    FakePrinterPortDevice device;
    peripheral.attach_printer_port_device(device);

    auto tick = [&] {
        via.update_phi2_trailing_edge();
        via.update_phi2_leading_edge();
    };

    via.write(Via6522::REG_DDRA, 0xFF);
    via.write(Via6522::REG_PCR, 0x0E);
    for (int i = 0; i < 100; ++i) tick();
    via.write(Via6522::REG_PCR, 0x0A);
    for (int i = 0; i < 100; ++i) tick();
    REQUIRE(device.strobe_count == 0);

    via.write(Via6522::REG_ORA, 0x41);
    for (int i = 0; i < 10; ++i) tick();
    REQUIRE(device.strobe_count == 1);
    REQUIRE(device.latched_data == 0x41);
}

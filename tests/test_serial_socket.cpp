// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
// Copyright 2026 Mark J. Fisher
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

#include "SerialTestDevice.hpp"

#include <beebium/ModelBHardware.hpp>
#include <beebium/extension/SerialPort.hpp>
#include <beebium/serial/SerialConcepts.hpp>
#include <beebium/serial/SerialSocket.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace beebium;

namespace {

// 8N1 at the /64 counter divide, which is what MOS 1.20 uses on RS423: the
// Serial ULA supplies 64 receive and transmit clocks per bit at the selected rate.
constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_64
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

// Phase 2 / Correction 1, Axis A: serial-socket presence is gated by detecting
// the serial_socket member, so a hardware variant that does not fit the socket
// (a future Master Compact without serial) is correctly excluded -- the rest of
// the stack guards on this concept. Cassette is a separate axis, deliberately
// NOT modelled here.
struct NoSerialHardware {};
static_assert(HasSerialSocket<ModelBHardware>,
              "Model B fits the on-board serial socket");
static_assert(!HasSerialSocket<NoSerialHardware>,
              "hardware without serial_socket is excluded by the concept");

}  // namespace

// =============================================================================
// SerialSocket standalone
// =============================================================================

TEST_CASE("SerialSocket reset leaves TDRE ready and IRQ clear", "[serial][socket]") {
    SerialSocket socket;
    socket.write_acia(0, CONTROL_8N1);
    CHECK((socket.read_acia(0) & Mc6850::SR_TDRE) != 0);
    CHECK_FALSE(socket.irq_pending());
}

TEST_CASE("SerialSocket ULA write routes to the ACIA /DCD", "[serial][socket]") {
    SerialSocket socket;
    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    CHECK_FALSE(socket.acia().not_dcd());
}

// =============================================================================
// Memory-map integration through ModelBHardware
// =============================================================================

TEST_CASE("Model B maps the ACIA at &FE08/&FE09", "[serial][socket][modelb]") {
    ModelBHardware hw;

    // Control register write at &FE08, status read back at &FE08.
    hw.write(0xFE08, CONTROL_8N1);
    CHECK((hw.read(0xFE08) & Mc6850::SR_TDRE) != 0);

    // &FE08 is mirrored on the low bit across &FE08-&FE0F, so &FE0A reads the
    // same status register, while odd addresses hit the data register.
    CHECK(hw.read(0xFE0A) == hw.read(0xFE08));
}

TEST_CASE("Model B maps the Serial ULA at &FE10", "[serial][socket][modelb]") {
    ModelBHardware hw;
    hw.write(0xFE08, CONTROL_8N1);
    hw.write(0xFE10, SerialUla::RS423_SELECT);
    CHECK(hw.serial_socket.ula().rs423_selected());
    CHECK_FALSE(hw.serial_socket.acia().not_dcd());
}

TEST_CASE("Model B serial ACIA IRQ reaches the IRQ aggregator", "[serial][socket][modelb]") {
    ModelBHardware hw;

    // No serial interrupt initially.
    CHECK((hw.poll_irq() & 0x10) == 0);  // bit 4 = serial ACIA

    // Enable RX IRQ, present carrier, and clock in a received character at the
    // ACIA level so RDRF + RX IRQ assert.
    Mc6850& acia = hw.serial_socket.acia();
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE);
    acia.set_not_dcd(false);
    // 64 receive clocks a bit at the ACIA's /64.
    acia.receive_clocks(64, true);
    acia.receive_clocks(64, false);
    for (int i = 0; i < 8; ++i) acia.receive_clocks(64, ((0x5A >> i) & 1) != 0);
    acia.receive_clocks(64, true);

    REQUIRE(acia.irq_pending());
    CHECK((hw.poll_irq() & 0x10) != 0);
}

// =============================================================================
// Device end-to-end through the socket
// =============================================================================

TEST_CASE("SerialSocket transmits a byte to the device", "[serial][socket]") {
    SerialSocket socket;
    SerialTestDevice device;
    socket.set_device(&device);

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // 19200, carrier present
    socket.write_acia(1, 0x5A);                      // write TDR

    for (int i = 0; i < 4000 && device.sent().empty(); ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    REQUIRE(device.sent().size() == 1);
    CHECK(device.sent()[0] == 0x5A);
}

TEST_CASE("SerialSocket delivers a byte from the device to the Beeb", "[serial][socket]") {
    SerialSocket socket;
    SerialTestDevice device;
    socket.set_device(&device);

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    device.push_from_device(0x39);

    for (int i = 0; i < 6000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0x39);
}

// A single SerialPortDevice wired via set_device() serves both directions: an
// echoing device sends a transmitted byte back so the Beeb receives it.
TEST_CASE("SerialSocket set_device loopback round-trips a byte", "[serial][socket]") {
    SerialSocket socket;
    SerialTestDevice device(/*echo=*/true);
    socket.set_device(&device);  // non-owning; device outlives the socket use

    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);  // 19200, carrier present
    socket.write_acia(1, 0xC3);                      // transmit

    for (int i = 0; i < 20000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }

    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0xC3);
}

// =============================================================================
// SerialPort handle (the UserPort analogue)
// =============================================================================

TEST_CASE("SerialPort handle attaches a single device and round-trips", "[serial][port]") {
    SerialSocket socket;
    SerialPort port(socket);
    CHECK_FALSE(port.is_occupied());

    SerialTestDevice endpoint(/*echo=*/true);
    port.attach(endpoint);
    CHECK(port.is_occupied());

    // The serial port has a single connector: a second attach throws.
    SerialTestDevice other;
    CHECK_THROWS(port.attach(other));

    // The attached device drives the round-trip through the socket/ULA.
    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);
    socket.write_acia(1, 0xC3);
    for (int i = 0; i < 20000 && (socket.read_acia(0) & Mc6850::SR_RDRF) == 0; ++i) {
        socket.tick_rising();
        socket.tick_falling();
    }
    REQUIRE((socket.read_acia(0) & Mc6850::SR_RDRF) != 0);
    CHECK(socket.read_acia(1) == 0xC3);

    port.detach();
    CHECK_FALSE(port.is_occupied());
}

// =============================================================================
// TX back-pressure (/CTS)
// =============================================================================

// The ULA drives the ACIA's /CTS input from the device's accepts_more() each TX
// bit period: when the device stops accepting, /CTS is asserted, TDRE reads 0,
// and the guest's transmit loop busy-waits until the device is ready again. (The
// per-device bounding that backs accepts_more lives with each device and is
// tested there.)
TEST_CASE("SerialSocket drives /CTS from device back-pressure", "[serial][socket]") {
    SerialSocket socket;
    SerialTestDevice device;
    socket.set_device(&device);
    socket.write_acia(0, CONTROL_8N1);
    socket.write_ula(0, SerialUla::RS423_SELECT);

    auto run_a_tx_bit_period = [&] {
        for (int t = 0; t < 200; ++t) { socket.tick_rising(); socket.tick_falling(); }
    };

    run_a_tx_bit_period();
    CHECK_FALSE(socket.acia().not_cts());                 // clear to send initially
    CHECK((socket.read_acia(0) & Mc6850::SR_TDRE) != 0);  // TDRE available

    device.set_accepts_more(false);                       // device back-pressures
    run_a_tx_bit_period();                                // pump_transmit re-evaluates /CTS
    CHECK(socket.acia().not_cts());                       // /CTS asserted
    CHECK((socket.read_acia(0) & Mc6850::SR_TDRE) == 0);  // TDRE masked -> guest stalls

    device.set_accepts_more(true);                        // device ready again
    run_a_tx_bit_period();
    CHECK_FALSE(socket.acia().not_cts());                 // /CTS released
    CHECK((socket.read_acia(0) & Mc6850::SR_TDRE) != 0);  // TDRE returns
}

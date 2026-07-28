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

#include <beebium/devices/Mc6850.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>

using namespace beebium;

namespace {

constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_16
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

// Control-register value for a given Word Select code (the datasheet order:
// 0=7E2, 1=7O2, 2=7E1, 3=7O1, 4=8N2, 5=8N1, 6=8E1, 7=8O1).
constexpr uint8_t control_for_word_select(uint8_t word_select) {
    return static_cast<uint8_t>(Mc6850::COUNTER_DIVIDE_16
        | (word_select << Mc6850::CR_WORD_SELECT_SHIFT));
}

// Clock the ULA until the ACIA has a received byte, or the budget runs out.
bool receive_byte(SerialUla& ula, Mc6850& acia, int max_ticks = 20000) {
    for (int i = 0; i < max_ticks && !acia.rdrf(); ++i) {
        ula.tick();
    }
    return acia.rdrf();
}

// RS423 selected, 19200 baud on both TX and RX (baud-select index 0).
constexpr uint8_t ULA_RS423_19200 = SerialUla::RS423_SELECT | 0x00;

}  // namespace

// =============================================================================
// Control-latch decode
// =============================================================================

TEST_CASE("SerialUla decodes baud-rate selects", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);

    ula.write(0, 0x00);  // tx=0 (19200), rx=0 (19200)
    CHECK(ula.tx_baud() == 19200);
    CHECK(ula.rx_baud() == 19200);

    // tx select = 4 (9600), rx select = 1 (1200)
    ula.write(0, (4 << 0) | (1 << SerialUla::RX_BAUD_SHIFT));
    CHECK(ula.tx_baud() == 9600);
    CHECK(ula.rx_baud() == 1200);
}

TEST_CASE("SerialUla RS423 select drives ACIA /DCD", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);

    ula.write(0, 0x00);                       // cassette select
    CHECK(acia.not_dcd());                     // /DCD high -> no carrier
    ula.write(0, SerialUla::RS423_SELECT);     // RS423 select
    CHECK_FALSE(acia.not_dcd());               // /DCD low -> carrier present
}

// =============================================================================
// RTS forwarding to the device (the BBC's outbound handshake line)
// =============================================================================

namespace {

// Control-register value selecting the given RTS state with 8N1 framing.
// tx_control field (bits 5-6): RTS high (deasserted) needs TX_CTRL_RTS_HIGH;
// any other value (here the default 0) holds RTS low (asserted).
constexpr uint8_t CONTROL_RTS_HIGH = CONTROL_8N1
    | (Mc6850::TX_CTRL_RTS_HIGH_NO_IRQ << Mc6850::CR_TX_CONTROL_SHIFT);

}  // namespace

TEST_CASE("SerialUla delivers the RTS baseline when a device attaches",
          "[serial][ula][rts]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;

    // Power-on control register (0) selects tx_control 0 -> RTS asserted.
    ula.set_sink(&device);

    REQUIRE(device.rts_events().size() == 1);
    CHECK(device.rts_events()[0] == true);
}

TEST_CASE("SerialUla forwards RTS transitions, one event per change",
          "[serial][ula][rts]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_sink(&device);  // baseline: asserted (true)

    // Deassert RTS via the ACIA control register, then tick.
    acia.write_control(CONTROL_RTS_HIGH);
    ula.tick();
    REQUIRE(device.rts_events().size() == 2);
    CHECK(device.rts_events()[1] == false);

    // Ticking again with no change must not produce further events.
    for (int i = 0; i < 500; ++i) ula.tick();
    CHECK(device.rts_events().size() == 2);

    // Re-assert RTS, then tick.
    acia.write_control(CONTROL_8N1);  // tx_control 0 -> RTS low (asserted)
    ula.tick();
    REQUIRE(device.rts_events().size() == 3);
    CHECK(device.rts_events()[2] == true);
}

TEST_CASE("SerialUla does not call set_rts before a device is attached",
          "[serial][ula][rts]") {
    Mc6850 acia;
    SerialUla ula(acia);
    // No sink attached: ticking and toggling RTS must not crash or record.
    acia.write_control(CONTROL_RTS_HIGH);
    for (int i = 0; i < 100; ++i) ula.tick();

    SerialTestDevice device;
    ula.set_sink(&device);
    REQUIRE(device.rts_events().size() == 1);
    CHECK(device.rts_events()[0] == false);  // baseline reflects current (high)
}

// =============================================================================
// BREAK forwarding (both directions) -- a sub-byte line condition carried out
// of band, not through the byte queue.
// =============================================================================

namespace {

// Control-register value that holds the transmitter in BREAK (the space state)
// with 8N1 framing: Transmitter-Control field (bits 5-6) = 11.
constexpr uint8_t CONTROL_BREAK = CONTROL_8N1
    | (Mc6850::TX_CTRL_RTS_LOW_BREAK << Mc6850::CR_TX_CONTROL_SHIFT);

}  // namespace

TEST_CASE("SerialUla delivers the BREAK baseline when a device attaches",
          "[serial][ula][break]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;

    // Power-on control register (0) is not a break.
    ula.set_sink(&device);

    REQUIRE(device.break_events().size() == 1);
    CHECK(device.break_events()[0] == false);
}

TEST_CASE("SerialUla forwards BREAK transitions, one event per change",
          "[serial][ula][break]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_sink(&device);  // baseline: not break (false)

    // Assert BREAK via the ACIA control register, then tick.
    acia.write_control(CONTROL_BREAK);
    ula.tick();
    REQUIRE(device.break_events().size() == 2);
    CHECK(device.break_events()[1] == true);

    // Ticking again with no change must not produce further events.
    for (int i = 0; i < 500; ++i) ula.tick();
    CHECK(device.break_events().size() == 2);

    // Clear BREAK, then tick.
    acia.write_control(CONTROL_8N1);
    ula.tick();
    REQUIRE(device.break_events().size() == 3);
    CHECK(device.break_events()[2] == false);
}

TEST_CASE("SerialUla injects an inbound BREAK as a Framing Error with data 0x00",
          "[serial][ula][break]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);   // carrier present
    device.push_break_from_device();

    for (int i = 0; i < 6000 && !acia.rdrf(); ++i) {
        ula.tick();
    }

    REQUIRE(acia.rdrf());
    // A received break is the MC6850's framing-error-with-zero-data condition.
    CHECK((acia.status() & Mc6850::SR_FE) != 0);
    CHECK(acia.read_data() == 0x00);
}

TEST_CASE("SerialUla decodes cassette motor bit", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    CHECK_FALSE(ula.motor_on());
    ula.write(0, SerialUla::MOTOR_ENABLE);
    CHECK(ula.motor_on());
}

TEST_CASE("SerialUla bit period derives from baud rate", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    ula.write(0, 0x00);  // 19200 baud
    // CPU_HZ / 19200 == 104 ticks per bit.
    CHECK(ula.tx_bit_period() == 104);
    CHECK(ula.rx_bit_period() == 104);
}

// =============================================================================
// Byte <-> bit shifting through the ACIA
// =============================================================================

TEST_CASE("SerialUla shifts a transmitted byte to the sink", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_sink(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);
    acia.write_data(0x53);  // 'S'

    // Clock well past one character time (10 bits * 104 ticks/bit).
    for (int i = 0; i < 4000 && device.sent().empty(); ++i) {
        ula.tick();
    }

    REQUIRE_FALSE(device.sent().empty());
    CHECK(device.sent()[0] == 0x53);
}

TEST_CASE("SerialUla shifts a received byte into the ACIA", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);   // carrier present
    device.push_from_device(0x6A);

    for (int i = 0; i < 6000 && !acia.rdrf(); ++i) {
        ula.tick();
    }

    REQUIRE(acia.rdrf());
    CHECK(acia.read_data() == 0x6A);
}

TEST_CASE("SerialUla receives in every ACIA word format", "[serial][ula]") {
    // The receive framing must follow the ACIA's Word Select field, not a fixed
    // 8N1 frame: viewdata software (Commstar's Prestel mode) selects 7E1, and a
    // frame-length disagreement between the ULA and the ACIA loses nearly every
    // byte.
    struct Format {
        uint8_t word_select;
        const char* name;
        uint8_t sent;
        uint8_t expected;  // 7-bit formats carry only the low seven bits
    };

    const Format format = GENERATE(
        Format{0, "7E2", 0x6A, 0x6A},
        Format{1, "7O2", 0x6A, 0x6A},
        Format{2, "7E1", 0x6A, 0x6A},
        Format{3, "7O1", 0x6A, 0x6A},
        Format{4, "8N2", 0xC3, 0xC3},
        Format{5, "8N1", 0xC3, 0xC3},
        Format{6, "8E1", 0xC3, 0xC3},
        Format{7, "8O1", 0xC3, 0xC3});

    INFO("word format " << format.name);

    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);

    acia.write_control(control_for_word_select(format.word_select));
    ula.write(0, ULA_RS423_19200);  // carrier present
    device.push_from_device(format.sent);

    REQUIRE(receive_byte(ula, acia));
    CHECK(acia.read_data() == format.expected);
    // Correct framing and parity: no error flags raised.
    CHECK_FALSE((acia.read_status() & Mc6850::SR_FE) != 0);
    CHECK_FALSE((acia.read_status() & Mc6850::SR_PE) != 0);
}

TEST_CASE("SerialUla receives consecutive bytes in a 7-bit parity format",
          "[serial][ula]") {
    // A framing mismatch tends to resynchronise by luck every few bytes, so a
    // single successful byte is not enough to prove the frame is right.
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);

    acia.write_control(control_for_word_select(2));  // 7E1
    ula.write(0, ULA_RS423_19200);

    const uint8_t message[] = {'N', 'i', 'g', 'h', 't', 0x00, 0x7F, 0x2A};
    for (uint8_t byte : message) {
        device.push_from_device(byte);
    }

    for (uint8_t expected : message) {
        REQUIRE(receive_byte(ula, acia));
        CHECK(acia.read_data() == expected);
        CHECK_FALSE((acia.read_status() & Mc6850::SR_PE) != 0);
        CHECK_FALSE((acia.read_status() & Mc6850::SR_FE) != 0);
    }
}

TEST_CASE("SerialUla full loopback echoes transmitted bytes back", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device(/*echo=*/true);
    ula.set_sink(&device);
    ula.set_source(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_RS423_19200);
    acia.write_data(0xC3);

    for (int i = 0; i < 20000 && !acia.rdrf(); ++i) {
        ula.tick();
    }

    REQUIRE(acia.rdrf());
    CHECK(acia.read_data() == 0xC3);
}

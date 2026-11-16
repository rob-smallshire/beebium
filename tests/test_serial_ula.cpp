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
#include <beebium/serial/SerialSocket.hpp>
#include <beebium/serial/SerialUla.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <cstdint>

using namespace beebium;

namespace {

// 8N1 at the /64 counter divide, which is what MOS 1.20 uses on RS423: the
// Serial ULA supplies 64 receive and transmit clocks per bit at the selected rate.
constexpr uint8_t CONTROL_8N1 = Mc6850::COUNTER_DIVIDE_64
    | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT);

// Control-register value for a given Word Select code (the datasheet order:
// 0=7E2, 1=7O2, 2=7E1, 3=7O1, 4=8N2, 5=8N1, 6=8E1, 7=8O1).
constexpr uint8_t control_for_word_select(uint8_t word_select) {
    return static_cast<uint8_t>(Mc6850::COUNTER_DIVIDE_64
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

TEST_CASE("SerialUla holds ACIA /DCD low in RS423 and cassette modes", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);

    ula.write(0, 0x00);                        // cassette select
    CHECK_FALSE(acia.not_dcd());               // no high-tone detector yet: /DCD low
    ula.write(0, SerialUla::RS423_SELECT);     // RS423 select
    CHECK_FALSE(acia.not_dcd());               // carrier present
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

// =============================================================================
// The ULA clock and its dividers
// =============================================================================

TEST_CASE("SerialUla runs 16/13 MHz of ULA clocks from the 2 MHz tick", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    for (int i = 0; i < 2'000'000; ++i) ula.tick();
    const uint64_t clocks = ula.ula_clocks();
    CHECK(clocks >= 1'230'768);
    CHECK(clocks <= 1'230'770);
}

TEST_CASE("SerialUla RS423 clocks number 64 a bit at every rate", "[serial][ula]") {
    for (uint8_t select = 0; select < 8; ++select) {
        const unsigned baud = SerialUla::BAUD_RATES[select];
        CAPTURE(select, baud);

        Mc6850 acia;
        SerialUla ula(acia);
        SerialTestDevice device;
        ula.set_sink(&device);
        acia.write_control(CONTROL_8N1);
        ula.write(0, static_cast<uint8_t>(SerialUla::RS423_SELECT | select
                                          | (select << SerialUla::RX_BAUD_SHIFT)));

        // Receive and transmit clocks are the ULA clock divided by the rate's
        // divider: 64 times the nominal rate, 0.16% fast.
        const uint64_t ula_start = ula.ula_clocks();
        const uint64_t rx_start = ula.rx_clocks();
        const uint64_t tx_start = ula.tx_clocks();
        for (int i = 0; i < 400'000; ++i) ula.tick();
        const uint64_t ula_elapsed = ula.ula_clocks() - ula_start;
        const uint64_t divider = SerialUla::DIVIDERS[select];
        CHECK(ula.rx_clocks() - rx_start == ula_elapsed / divider);
        CHECK(ula.tx_clocks() - tx_start == ula_elapsed / divider);

        // A stream of transmitted 8N1 bytes: 640 transmit clocks a byte, and
        // the byte rate is the table rate times 1.0016.
        const size_t bytes = baud >= 2400 ? 60 : 12;
        uint64_t first_tick = 0;
        uint64_t first_tx = 0;
        uint64_t last_tick = 0;
        uint64_t last_tx = 0;
        size_t seen = 0;
        size_t written = 0;
        uint64_t tick = 0;
        while (seen < bytes + 1 && tick < 40'000'000) {
            if (acia.tdre() && written < bytes + 2) {
                acia.write_data(static_cast<uint8_t>(0x30 + written));
                ++written;
            }
            ula.tick();
            ++tick;
            if (device.sent().size() > seen) {
                seen = device.sent().size();
                if (seen == 1) {
                    first_tick = tick;
                    first_tx = ula.tx_clocks();
                }
                last_tick = tick;
                last_tx = ula.tx_clocks();
            }
        }
        REQUIRE(seen == bytes + 1);
        CHECK(last_tx - first_tx == 640 * bytes);
        const double seconds = static_cast<double>(last_tick - first_tick) / 2'000'000.0;
        const double byte_rate = static_cast<double>(bytes) / seconds;
        const double expected = baud / 10.0 * 1.0016;
        CHECK(std::abs(byte_rate / expected - 1.0) < 1e-4);
    }
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

TEST_CASE("SerialUla RS423 byte is not received by an ACIA at /16", "[serial][ula]") {
    // The far end runs at the selected baud rate and knows nothing of the ACIA's
    // divider: each level lasts 64 receive clocks, so /16 samples garbage.
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);

    acia.write_control(static_cast<uint8_t>(Mc6850::COUNTER_DIVIDE_16
        | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT)));
    ula.write(0, ULA_RS423_19200);
    device.push_from_device(0x6A);

    bool received_intact = false;
    for (int i = 0; i < 20000; ++i) {
        ula.tick();
        if (acia.rdrf()) {
            const bool clean = (acia.read_status() & (Mc6850::SR_FE | Mc6850::SR_PE)) == 0;
            if (acia.read_data() == 0x6A && clean) received_intact = true;
        }
    }
    CHECK_FALSE(received_intact);
}

// =============================================================================
// Reading &FE10
// =============================================================================

TEST_CASE("SerialUla read acts as a write of FE and returns open bus", "[serial][ula]") {
    SerialSocket socket;
    const uint8_t bus = 0x5A;
    socket.set_last_bus_value_ptr(&bus);
    socket.write_ula(0, 0x05);  // cassette, motor off, transmit 300

    // &FE is %11111110: motor on, RS423 selected (bit 6), receive 75 (111),
    // transmit 2400 (110).
    CHECK(socket.read_ula(0) == 0x5A);
    CHECK(socket.ula().control() == 0xFE);
    CHECK(socket.ula().motor_on());
    CHECK(socket.ula().rs423_selected());
    CHECK(socket.ula().rx_baud() == 75);
    CHECK(socket.ula().tx_baud() == 2400);
}

TEST_CASE("SerialUla peek has no side effect", "[serial][ula]") {
    SerialSocket socket;
    const uint8_t bus = 0x33;
    socket.set_last_bus_value_ptr(&bus);
    socket.write_ula(0, SerialUla::RS423_SELECT);

    CHECK(socket.peek_ula(0) == 0x33);
    CHECK(socket.ula().control() == SerialUla::RS423_SELECT);
}

// =============================================================================
// Cassette selected
// =============================================================================

namespace {

// Cassette selected, motor on, transmit 300 (the MOS's tape setting: divider
// 64), receive field 0.
constexpr uint8_t ULA_CASSETTE_300 = SerialUla::MOTOR_ENABLE | 0x05;

}  // namespace

TEST_CASE("SerialUla in cassette mode holds /DCD and /CTS low and carries no bytes",
          "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    device.set_accepts_more(false);  // would raise /CTS on RS423
    ula.set_source(&device);
    ula.set_sink(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_CASSETTE_300);
    CHECK_FALSE(acia.not_dcd());
    CHECK_FALSE(acia.not_cts());

    // A byte offered by the RS423 source is not received.
    device.push_from_device(0x6A);
    const uint64_t rx_before = ula.rx_clocks();
    for (int i = 0; i < 200'000; ++i) ula.tick();
    CHECK_FALSE(acia.rdrf());
    CHECK(ula.rx_clocks() == rx_before);  // no receive clock until round 4
    CHECK_FALSE(acia.not_cts());

    // A transmitted byte does not reach the sink, and the next character's
    // TDRE comes back 10 bits of the transmit divider after the first starts.
    device.set_accepts_more(true);
    acia.write_data(0x41);
    while (!acia.tdre()) ula.tick();          // the first character has started
    acia.write_data(0x42);
    const uint64_t start = ula.ula_clocks();
    while (!acia.tdre()) ula.tick();          // the second character has started
    const uint64_t elapsed = ula.ula_clocks() - start;
    const uint64_t ten_bits = 10ull * 64 * SerialUla::DIVIDERS[5];
    CHECK(elapsed + 1 >= ten_bits);
    CHECK(elapsed <= ten_bits + 1);
    for (int i = 0; i < 200'000; ++i) ula.tick();
    CHECK(device.sent().empty());
}

TEST_CASE("SerialUla switching back to RS423 restores carrier, /CTS and transport",
          "[serial][ula]") {
    Mc6850 acia;
    SerialUla ula(acia);
    SerialTestDevice device;
    ula.set_source(&device);
    ula.set_sink(&device);

    acia.write_control(CONTROL_8N1);
    ula.write(0, ULA_CASSETTE_300);
    ula.write(0, ULA_RS423_19200);
    CHECK_FALSE(acia.not_dcd());

    device.set_accepts_more(false);
    for (int i = 0; i < 1000; ++i) ula.tick();
    CHECK(acia.not_cts());
    device.set_accepts_more(true);
    for (int i = 0; i < 1000; ++i) ula.tick();
    CHECK_FALSE(acia.not_cts());

    device.push_from_device(0x6A);
    REQUIRE(receive_byte(ula, acia));
    CHECK(acia.read_data() == 0x6A);

    acia.write_data(0x53);
    for (int i = 0; i < 4000 && device.sent().empty(); ++i) ula.tick();
    REQUIRE_FALSE(device.sent().empty());
    CHECK(device.sent()[0] == 0x53);
}

// =============================================================================
// The part
// =============================================================================

TEST_CASE("SerialUla part defaults to Ferranti", "[serial][ula]") {
    Mc6850 acia;
    SerialUla ferranti(acia);
    CHECK(ferranti.part() == SerialUla::Part::Ferranti);

    Mc6850 acia2;
    SerialUla vlsi(acia2, SerialUla::Part::VLSI);
    CHECK(vlsi.part() == SerialUla::Part::VLSI);
}

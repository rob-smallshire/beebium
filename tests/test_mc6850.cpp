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

#include <beebium/devices/Mc6850.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

using namespace beebium;

namespace {

// Word select 0b101 = 8N1, with the given counter divide.
constexpr uint8_t control_8n1(uint8_t divide) {
    return static_cast<uint8_t>(divide | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT));
}

// 8N1 at /16: the unit tests below clock the ACIA directly at 16 clocks a bit.
constexpr uint8_t CONTROL_8N1 = control_8n1(Mc6850::COUNTER_DIVIDE_16);

constexpr uint8_t control_for(uint8_t divide, uint8_t word_select) {
    return static_cast<uint8_t>(divide | (word_select << Mc6850::CR_WORD_SELECT_SHIFT));
}

unsigned clocks_per_bit(uint8_t divide) {
    switch (divide) {
        case Mc6850::COUNTER_DIVIDE_1: return 1;
        case Mc6850::COUNTER_DIVIDE_16: return 16;
        default: return 64;
    }
}

struct WordFormat {
    uint8_t word_select;
    unsigned data_bits;
    Mc6850::Parity parity;
    unsigned stop_bits;
};

// The datasheet's Word Select order.
constexpr WordFormat WORD_FORMATS[8] = {
    {0, 7, Mc6850::Parity::Even, 2}, {1, 7, Mc6850::Parity::Odd, 2},
    {2, 7, Mc6850::Parity::Even, 1}, {3, 7, Mc6850::Parity::Odd, 1},
    {4, 8, Mc6850::Parity::None, 2}, {5, 8, Mc6850::Parity::None, 1},
    {6, 8, Mc6850::Parity::Even, 1}, {7, 8, Mc6850::Parity::Odd, 1},
};

// The line levels of one frame: start, data LSB first, parity, stop bits.
std::vector<bool> frame_levels(uint8_t value, const WordFormat& format, bool stop_level = true) {
    std::vector<bool> levels;
    levels.push_back(false);
    unsigned ones = 0;
    for (unsigned i = 0; i < format.data_bits; ++i) {
        const bool bit = ((value >> i) & 1) != 0;
        ones += bit ? 1 : 0;
        levels.push_back(bit);
    }
    if (format.parity == Mc6850::Parity::Even) levels.push_back((ones & 1) != 0);
    if (format.parity == Mc6850::Parity::Odd) levels.push_back((ones & 1) == 0);
    for (unsigned i = 0; i < format.stop_bits; ++i) levels.push_back(stop_level);
    return levels;
}

// Present each level on RxD for the given number of receive clocks.
void clock_levels(Mc6850& acia, const std::vector<bool>& levels, unsigned clocks_each) {
    for (bool level : levels) {
        for (unsigned c = 0; c < clocks_each; ++c) {
            acia.receive_clock(level);
        }
    }
}

// Clock a byte into the receiver at 16 clocks a bit, 8N1, after a bit time of
// idle line. Assumes /DCD low.
void receive_byte(Mc6850& acia, uint8_t value, bool stop_level = true) {
    clock_levels(acia, {true}, 16);
    clock_levels(acia, frame_levels(value, WORD_FORMATS[5], stop_level), 16);
}

// Run transmit clocks until a whole character has been reported, returning the
// assembled data byte (LSB first).
uint8_t transmit_byte(Mc6850& acia, unsigned max_clocks = 64 * 16) {
    uint8_t assembled = 0;
    int bit_index = 0;
    bool seen_start = false;
    for (unsigned i = 0; i < max_clocks; ++i) {
        const std::optional<Mc6850::TransmitResult> r = acia.transmit_clock();
        if (!r) continue;
        if (r->type == Mc6850::BitType::Start) {
            seen_start = true;
            bit_index = 0;
            assembled = 0;
        } else if (r->type == Mc6850::BitType::Data && seen_start) {
            if (r->bit) assembled |= static_cast<uint8_t>(1u << bit_index);
            ++bit_index;
        } else if (r->type == Mc6850::BitType::Stop && seen_start) {
            break;
        }
    }
    REQUIRE(seen_start);
    return assembled;
}

}  // namespace

// =============================================================================
// Reset / power-on state
// =============================================================================

TEST_CASE("ACIA power-on: TDRE set, RDRF clear", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
    CHECK((acia.status() & Mc6850::SR_RDRF) == 0);
    CHECK_FALSE(acia.irq_pending());
}

TEST_CASE("ACIA master reset clears status and suppresses IRQ/TDRE", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    // Master reset (counter divide = 11).
    acia.write_control(Mc6850::COUNTER_DIVIDE_MASTER_RESET);
    // While in master reset, TDRE and IRQ are forced low.
    CHECK((acia.status() & Mc6850::SR_TDRE) == 0);
    CHECK((acia.status() & Mc6850::SR_IRQ) == 0);
    CHECK_FALSE(acia.irq_pending());
}

TEST_CASE("ACIA control write mid-character leaves the character and the TDR alone", "[serial][mc6850]") {
    // MOS 1.20 writes &35 (transmit interrupt on, /RTS low) at the start of
    // each cassette block with the sync byte &2A already in the shift
    // register. Only a master reset touches the transmitter: the character in
    // flight finishes and the byte waiting in the TDR follows it, once each.
    Mc6850 acia;
    acia.write_control(control_for(Mc6850::COUNTER_DIVIDE_16, 5));  // &15: 8N1, /16
    acia.write_data(0x2A);
    std::vector<uint8_t> bits;
    unsigned clocks = 0;
    auto run = [&](unsigned n) {
        for (unsigned i = 0; i < n; ++i, ++clocks) {
            if (const auto r = acia.transmit_clock()) bits.push_back(r->bit);
        }
    };
    run(16 * 3);                   // the start bit and two data bits of &2A
    acia.write_data(0x50);         // "P" waits in the TDR
    CHECK((acia.status() & Mc6850::SR_TDRE) == 0);
    acia.write_control(0x35);      // mid-character: transmit interrupt on
    run(16 * 30);
    // Decode the line: frames of start, eight data bits LSB first, stop.
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 10 <= bits.size();) {
        if (bits[i] != 0) { ++i; continue; }
        uint8_t v = 0;
        for (int b = 0; b < 8; ++b) v |= static_cast<uint8_t>(bits[i + 1 + b] << b);
        CHECK(bits[i + 9] == 1);
        bytes.push_back(v);
        i += 10;
    }
    CHECK(bytes == std::vector<uint8_t>{0x2A, 0x50});
    CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
}

TEST_CASE("ACIA master reset mid-character abandons it and holds TxD at mark", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(control_for(Mc6850::COUNTER_DIVIDE_16, 5));  // 8N1
    acia.write_data(0x00);
    // Into the character: its start bit and the first data bit, both space.
    unsigned spaces = 0;
    for (unsigned clock = 0; clock < 16 * 2; ++clock) {
        const auto r = acia.transmit_clock();
        if (r && r->bit == 0) ++spaces;
    }
    REQUIRE(spaces == 2);

    acia.write_control(Mc6850::COUNTER_DIVIDE_MASTER_RESET);
    // Mark on every clock for as long as the reset lasts.
    for (unsigned clock = 0; clock < 16 * 40; ++clock) {
        const auto r = acia.transmit_clock();
        REQUIRE(r);
        CHECK(r->bit == 1);
        CHECK(r->type == Mc6850::BitType::Idle);
    }

    // Out of reset the character is gone: TDRE is set and the line idles.
    acia.write_control(control_for(Mc6850::COUNTER_DIVIDE_16, 5));
    CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
    for (unsigned clock = 0; clock < 16 * 20; ++clock) {
        const auto r = acia.transmit_clock();
        if (r) CHECK(r->type == Mc6850::BitType::Idle);
    }
}

TEST_CASE("ACIA master reset clears the control register", "[serial][mc6850]") {
    Mc6850 acia;
    // Transmitter control 10 (/RTS high) and the receive interrupt enabled.
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE
        | (Mc6850::TX_CTRL_RTS_HIGH_NO_IRQ << Mc6850::CR_TX_CONTROL_SHIFT));
    REQUIRE(acia.get_not_rts());

    acia.write_control(Mc6850::COUNTER_DIVIDE_MASTER_RESET);
    CHECK(acia.control() == 0);
    CHECK_FALSE(acia.get_not_rts());  // RTS low

    // A byte offered while still in master reset is not received.
    receive_byte(acia, 0x42);
    CHECK_FALSE(acia.rdrf());
    CHECK_FALSE(acia.irq_pending());

    // Leaving reset without CR7: a received byte sets RDRF but raises no IRQ.
    acia.write_control(CONTROL_8N1);
    receive_byte(acia, 0x42);
    CHECK(acia.rdrf());
    CHECK_FALSE(acia.irq_pending());
    CHECK(acia.read_data() == 0x42);

    // With CR7 written again, the next byte raises the IRQ.
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE);
    receive_byte(acia, 0x43);
    CHECK(acia.irq_pending());
}

// =============================================================================
// Control register decode
// =============================================================================

TEST_CASE("ACIA decodes 8N1 word format", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    CHECK(acia.word_select() == 0x05);
    CHECK(acia.eight_bit());
    CHECK(acia.parity() == Mc6850::Parity::None);
    CHECK(acia.one_stop_bit());
}

TEST_CASE("ACIA /RTS reflects transmitter control field", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);  // tx control 00 -> /RTS low
    CHECK_FALSE(acia.get_not_rts());
    acia.write_control(CONTROL_8N1
        | (Mc6850::TX_CTRL_RTS_HIGH_NO_IRQ << Mc6850::CR_TX_CONTROL_SHIFT));
    CHECK(acia.get_not_rts());
}

// =============================================================================
// Transmit path
// =============================================================================

TEST_CASE("ACIA transmits a byte LSB first", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    acia.write_data(0x41);
    CHECK((acia.status() & Mc6850::SR_TDRE) == 0);  // TDRE cleared by loading TDR

    CHECK(transmit_byte(acia) == 0x41);
    // After the byte is latched into the shift register, TDRE comes back.
    CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
}

TEST_CASE("ACIA TX IRQ asserted only when transmit interrupt enabled", "[serial][mc6850]") {
    Mc6850 acia;
    // Transmitter control 01 = /RTS low, TX IRQ enabled.
    acia.write_control(CONTROL_8N1
        | (Mc6850::TX_CTRL_RTS_LOW_IRQ << Mc6850::CR_TX_CONTROL_SHIFT));

    acia.write_data(0x55);
    // Clock to the first bit boundary: the start bit latches the byte and
    // raises TX IRQ.
    for (int i = 0; i < 16; ++i) acia.transmit_clock();
    CHECK(acia.irq_pending());
    CHECK((acia.status() & Mc6850::SR_IRQ) != 0);
}

TEST_CASE("ACIA transmit at /64 frames a character in 64 clocks a bit",
          "[serial][mc6850]") {
    // 8N1 (10 bits), 8N2 (11 bits) and 7E1 (start, 7 data, parity, stop: 10).
    struct Case { uint8_t word_select; unsigned frame_bits; };
    for (const Case c : {Case{5, 10}, Case{4, 11}, Case{2, 10}}) {
        const WordFormat& format = WORD_FORMATS[c.word_select];
        const unsigned frame_bits = c.frame_bits;
        CAPTURE(c.word_select, frame_bits);

        Mc6850 acia;
        acia.write_control(control_for(Mc6850::COUNTER_DIVIDE_64, c.word_select));
        acia.write_data(0xA5);

        std::vector<std::pair<unsigned, Mc6850::TransmitResult>> boundaries;
        bool second_written = false;
        for (unsigned clock = 1; clock <= 64 * 30; ++clock) {
            const auto r = acia.transmit_clock();
            if (!r) continue;
            boundaries.emplace_back(clock, *r);
            if (r->type == Mc6850::BitType::Start && !second_written) {
                // TDRE sets at the start of the character.
                CHECK((acia.status() & Mc6850::SR_TDRE) != 0);
                acia.write_data(0x3C);
                second_written = true;
            }
        }

        // Bit boundaries are 64 clocks apart.
        for (size_t i = 1; i < boundaries.size(); ++i) {
            CHECK(boundaries[i].first - boundaries[i - 1].first == 64);
        }

        // The two characters' start bits are a whole frame apart.
        std::vector<unsigned> starts;
        for (const auto& [clock, r] : boundaries) {
            if (r.type == Mc6850::BitType::Start) starts.push_back(clock);
        }
        REQUIRE(starts.size() >= 2);
        CHECK(starts[1] - starts[0] == 64 * frame_bits);

        // The reported levels spell the first byte, LSB first, in its format.
        const std::vector<bool> expected = frame_levels(0xA5, format);
        size_t first = 0;
        while (boundaries[first].second.type != Mc6850::BitType::Start) ++first;
        for (size_t i = 0; i < expected.size(); ++i) {
            CAPTURE(i);
            CHECK((boundaries[first + i].second.bit != 0) == expected[i]);
        }
    }
}

// =============================================================================
// Receive path: division and start-bit hunting
// =============================================================================

TEST_CASE("ACIA receives every word format at its own divide", "[serial][mc6850]") {
    for (uint8_t divide : {Mc6850::COUNTER_DIVIDE_64, Mc6850::COUNTER_DIVIDE_16,
                           Mc6850::COUNTER_DIVIDE_1}) {
        for (const WordFormat& format : WORD_FORMATS) {
            CAPTURE(divide, format.word_select);
            Mc6850 acia;
            acia.write_control(control_for(divide, format.word_select));
            const unsigned n = clocks_per_bit(divide);
            const uint8_t value = format.data_bits == 8 ? 0xC5 : 0x45;

            clock_levels(acia, {true, true}, n);
            clock_levels(acia, frame_levels(value, format), n);
            clock_levels(acia, {true}, n);

            REQUIRE(acia.rdrf());
            CHECK((acia.status() & (Mc6850::SR_FE | Mc6850::SR_PE)) == 0);
            CHECK(acia.read_data() == value);
        }
    }
}

TEST_CASE("ACIA at /64 does not receive a byte framed at 16 clocks a bit",
          "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(control_8n1(Mc6850::COUNTER_DIVIDE_64));
    clock_levels(acia, {true}, 64);
    clock_levels(acia, frame_levels(0x55, WORD_FORMATS[5]), 16);
    clock_levels(acia, std::vector<bool>(12, true), 64);

    const bool received_intact = acia.rdrf()
        && (acia.status() & (Mc6850::SR_FE | Mc6850::SR_PE)) == 0
        && acia.read_data() == 0x55;
    CHECK_FALSE(received_intact);
}

TEST_CASE("ACIA ignores a low glitch shorter than half a bit", "[serial][mc6850]") {
    struct Case { uint8_t divide; unsigned glitch; };
    for (const Case c : {Case{Mc6850::COUNTER_DIVIDE_16, 7},
                         Case{Mc6850::COUNTER_DIVIDE_64, 31}}) {
        CAPTURE(c.divide, c.glitch);
        Mc6850 acia;
        acia.write_control(control_8n1(c.divide));
        const unsigned n = clocks_per_bit(c.divide);

        clock_levels(acia, {true}, n);
        clock_levels(acia, std::vector<bool>(c.glitch, false), 1);
        clock_levels(acia, std::vector<bool>(12, true), n);
        CHECK_FALSE(acia.rdrf());

        // The receiver is still hunting cleanly: a real byte arrives intact.
        clock_levels(acia, frame_levels(0x5A, WORD_FORMATS[5]), n);
        clock_levels(acia, {true}, n);
        REQUIRE(acia.rdrf());
        CHECK((acia.status() & Mc6850::SR_FE) == 0);
        CHECK(acia.read_data() == 0x5A);
    }
}

TEST_CASE("ACIA batched receive clocks sample as single clocks do", "[serial][mc6850]") {
    Mc6850 single;
    Mc6850 batched;
    single.write_control(control_8n1(Mc6850::COUNTER_DIVIDE_64));
    batched.write_control(control_8n1(Mc6850::COUNTER_DIVIDE_64));

    // Levels held for uneven run lengths, so batches straddle sample points.
    const std::vector<std::pair<bool, unsigned>> runs = {
        {true, 100}, {false, 64}, {true, 70}, {false, 58}, {true, 128},
        {false, 192}, {true, 300}, {false, 20}, {true, 200},
    };
    for (const auto& [level, count] : runs) {
        for (unsigned i = 0; i < count; ++i) single.receive_clock(level);
        batched.receive_clocks(count, level);
    }
    CHECK(single.status() == batched.status());
    CHECK(single.debug_read_data() == batched.debug_read_data());
}

TEST_CASE("ACIA receives a byte and sets RDRF", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    receive_byte(acia, 0x42);
    CHECK(acia.rdrf());
    CHECK((acia.status() & Mc6850::SR_RDRF) != 0);
    CHECK(acia.read_data() == 0x42);
    CHECK_FALSE(acia.rdrf());  // reading RDR clears RDRF
}

TEST_CASE("ACIA RX IRQ gated by receive interrupt enable", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE);

    receive_byte(acia, 0x42);
    CHECK(acia.irq_pending());
    acia.read_data();
    CHECK_FALSE(acia.irq_pending());  // reading RDR clears the RX IRQ cause
}

TEST_CASE("ACIA flags framing error on bad stop bit", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    receive_byte(acia, 0x7F, /*stop_level=*/false);  // stop bit driven low
    CHECK(acia.rdrf());
    CHECK((acia.status() & Mc6850::SR_FE) != 0);
}

TEST_CASE("ACIA flags overrun when RDR not read", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    receive_byte(acia, 0x11);   // first byte fills RDR
    receive_byte(acia, 0x22);   // second byte arrives before RDR is read

    // The first byte is preserved; overrun surfaces when RDR is read.
    CHECK(acia.read_data() == 0x11);
    CHECK((acia.status() & Mc6850::SR_OVRN) != 0);
}

TEST_CASE("ACIA overrun does not survive the next character", "[serial][mc6850]") {
    // "Character synchronization is maintained during the Overrun condition":
    // the receiver keeps delivering, so a character arriving after the gap is
    // presented with clean flags rather than inheriting the error.
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    receive_byte(acia, 0x11);
    receive_byte(acia, 0x22);   // arrives before RDR is read: overrun
    CHECK(acia.read_data() == 0x11);
    CHECK((acia.status() & Mc6850::SR_OVRN) != 0);

    receive_byte(acia, 0x33);   // a fresh character, after the guest caught up

    CHECK(acia.rdrf());
    CHECK((acia.status() & Mc6850::SR_OVRN) == 0);
    CHECK(acia.read_data() == 0x33);
}

TEST_CASE("ACIA overrun costs the second character, not the first",
          "[serial][mc6850]") {
    // Datasheet: "The overrun condition begins at the midpoint of the last bit
    // of the second character received in succession without a read of the RDR
    // having occurred." Double buffering buys one character time of slack while
    // the next character is in flight -- it is not an extra queue slot, so the
    // second completed character is lost and the first is preserved.
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    receive_byte(acia, 0x41);
    receive_byte(acia, 0x42);

    CHECK(acia.read_data() == 0x41);              // the older character survives
    CHECK((acia.status() & Mc6850::SR_OVRN) != 0);
}

TEST_CASE("ACIA overrun does not outlive the read that clears it",
          "[serial][mc6850]") {
    // The regression behind issue #59: a burst arriving with nobody reading
    // must not cause a later, unrelated character to be lost. Once the guest
    // has drained the receiver, a fresh character arrives with clean flags.
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);

    for (uint8_t b : {0x0D, 0x0A, 0x4F, 0x4B, 0x0D, 0x0A}) {  // "\r\nOK\r\n"
        receive_byte(acia, b);
    }
    // Drain the way a guest does: keep reading while RDRF is set. The overrun
    // is reported, and the guest is welcome to leave the flag standing -- it
    // has nothing more to read.
    for (int i = 0; i < 8 && acia.rdrf(); ++i) {
        acia.read_data();
    }
    CHECK_FALSE(acia.rdrf());

    receive_byte(acia, 0x41);  // 'A', long after the burst

    CHECK(acia.rdrf());
    CHECK((acia.status() & Mc6850::SR_OVRN) == 0);
    CHECK(acia.read_data() == 0x41);
}

// =============================================================================
// /DCD
// =============================================================================

TEST_CASE("ACIA holds receiver idle while /DCD high", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(true);  // no carrier

    receive_byte(acia, 0x42);
    CHECK_FALSE(acia.rdrf());  // nothing received without carrier
}

TEST_CASE("ACIA latches a /DCD rising edge until the data register is read",
          "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1 | Mc6850::CR_RX_IRQ_ENABLE);
    REQUIRE((acia.status() & Mc6850::SR_NOT_DCD) == 0);

    // A pulse: rising edge, then back low.
    acia.set_not_dcd(true);
    CHECK((acia.status() & Mc6850::SR_NOT_DCD) != 0);
    CHECK(acia.irq_pending());
    acia.set_not_dcd(false);

    // Status reads alone do not clear the latch.
    for (int i = 0; i < 3; ++i) {
        CHECK((acia.read_status() & Mc6850::SR_NOT_DCD) != 0);
    }
    CHECK(acia.irq_pending());

    // A data read clears it, and the IRQ with it.
    acia.read_data();
    CHECK((acia.status() & Mc6850::SR_NOT_DCD) == 0);
    CHECK_FALSE(acia.irq_pending());

    // A second rising edge after it has fallen sets it again.
    acia.set_not_dcd(true);
    acia.set_not_dcd(false);
    CHECK((acia.status() & Mc6850::SR_NOT_DCD) != 0);
    CHECK(acia.irq_pending());
}

TEST_CASE("ACIA without CR7 latches /DCD but raises no IRQ", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(true);
    acia.set_not_dcd(false);
    CHECK((acia.status() & Mc6850::SR_NOT_DCD) != 0);
    CHECK_FALSE(acia.irq_pending());
}

TEST_CASE("ACIA reports RDRF clear while /DCD is high", "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    receive_byte(acia, 0x42);
    REQUIRE(acia.rdrf());

    acia.set_not_dcd(true);
    CHECK((acia.status() & Mc6850::SR_RDRF) == 0);
    CHECK_FALSE(acia.rdrf());
}

TEST_CASE("ACIA /DCD held high reads as set after the data register is read",
          "[serial][mc6850]") {
    Mc6850 acia;
    acia.write_control(CONTROL_8N1);
    acia.set_not_dcd(true);
    acia.read_data();
    // The latch is clear, but the bit follows the pin while it stays high.
    CHECK((acia.status() & Mc6850::SR_NOT_DCD) != 0);
}

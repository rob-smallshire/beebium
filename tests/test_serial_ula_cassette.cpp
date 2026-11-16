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

// The cassette side of the Serial ULA (data separator, high-tone detector, tone
// synthesiser) and the cassette deck, in ULA clocks (cassette round 4, #208).

#include "support/tape_builder.hpp"

#include <beebium/Machines.hpp>
#include <beebium/cassette/CassetteDeck.hpp>
#include <beebium/cassette/TapeSink.hpp>
#include <beebium/cassette/UefTape.hpp>
#include <beebium/devices/Mc6850.hpp>
#include <beebium/serial/SerialUla.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <memory>
#include <vector>

using namespace beebium;
using beebium::test::TapeBuilder;

namespace {

// Cassette selected, motor on, transmit 300 (divider 64): what MOS 1.20 writes.
constexpr uint8_t ULA_CASSETTE_MOTOR_ON = SerialUla::MOTOR_ENABLE | 0x05;
constexpr uint8_t ULA_CASSETTE_MOTOR_OFF = 0x05;

constexpr uint8_t acia_8n1(uint8_t divide) {
    return static_cast<uint8_t>(divide | (0x05 << Mc6850::CR_WORD_SELECT_SHIFT));
}

// A tape of explicit edges, for driving the separator exactly.
class EdgeTape final : public TapeSignal {
public:
    explicit EdgeTape(std::vector<TapeItem> items) : items_(std::move(items)) {}
    TapeItem next() override {
        if (pos_ < items_.size()) return items_[pos_++];
        return TapeItem::end(items_.empty() ? 0 : items_.back().time + 1);
    }
    void seek(TapeTime time) override {
        pos_ = 0;
        while (pos_ < items_.size() && items_[pos_].time < time) ++pos_;
    }
    std::optional<TapeTime> duration() const override {
        return items_.empty() ? 0 : items_.back().time + 1;
    }
    TapeKind kind() const override { return TapeKind::Uef; }
    const TapeImageMetadata& metadata() const override { return metadata_; }

private:
    std::vector<TapeItem> items_;
    size_t pos_ = 0;
    TapeImageMetadata metadata_;
};

// Edges at `start`, then every `spacing` clocks, `count` in all.
void add_edges(std::vector<TapeItem>& items, TapeTime& t, uint32_t spacing, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        items.push_back(TapeItem::edge(t, (items.size() & 1) != 0));
        t += spacing;
    }
}

struct Rig {
    Mc6850 acia;
    SerialUla ula{acia};
    CassetteDeck deck;

    explicit Rig(SerialUla::Part part = SerialUla::Part::Ferranti) : ula(acia, part) {
        ula.set_deck(&deck);
    }

    void load(std::unique_ptr<TapeSignal> tape) {
        deck.insert(std::move(tape), "test");
        deck.play();
    }

    void load_items(std::vector<TapeItem> items) { load(std::make_unique<EdgeTape>(std::move(items))); }

    void load_uef(const TapeBuilder& builder) {
        const auto bytes = builder.uef();
        load(UefTape::open(bytes, "test.uef"));
    }

    // Run whole ULA clocks.
    void run(uint64_t clocks) {
        const uint64_t until = ula.ula_clocks() + clocks;
        while (ula.ula_clocks() < until) ula.tick();
    }

    // Run until the deck reaches `position` (or the budget runs out).
    void run_to(TapeTime position, uint64_t budget = 100'000'000) {
        while (deck.position() < position && budget-- > 0) ula.tick();
    }
};

class RecordingSink final : public TapeSink {
public:
    void edge(TapeTime time, bool) override { edges.push_back(time); }
    void silence_until(TapeTime) override { ++silences; }
    std::vector<TapeTime> edges;
    int silences = 0;
};

}  // namespace

// =============================================================================
// 6. The data separator
// =============================================================================

TEST_CASE("Separator gives four receive clocks an edge of 2400 Hz and RxD 1 from the third edge",
          "[serial_ula][cassette]") {
    Rig rig;
    rig.acia.write_control(acia_8n1(Mc6850::COUNTER_DIVIDE_16));
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    std::vector<TapeItem> items;
    TapeTime t = 1000;
    add_edges(items, t, 256, 40);
    rig.load_items(items);

    rig.run_to(1000 + 1);   // first edge taken
    CHECK_FALSE(rig.ula.cassette_rxd());  // after silence: a long gap, 0
    rig.run_to(1000 + 256 + 1);
    CHECK_FALSE(rig.ula.cassette_rxd());  // one short gap after a long one: hold
    rig.run_to(1000 + 512 + 1);
    CHECK(rig.ula.cassette_rxd());        // two short gaps: 1

    // Sixteen receive clocks for each 1200-baud bit (four edges).
    rig.run_to(1000 + 4 * 256);
    const uint64_t before = rig.ula.rx_clocks();
    rig.run_to(1000 + 8 * 256);
    CHECK(rig.ula.rx_clocks() - before == 16);
}

TEST_CASE("Separator times out on 1200 Hz, gives sixteen clocks a bit and RxD 0",
          "[serial_ula][cassette]") {
    Rig rig;
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    std::vector<TapeItem> items;
    TapeTime t = 1000;
    add_edges(items, t, 256, 8);    // some carrier first, so RxD is 1
    add_edges(items, t, 512, 20);
    rig.load_items(items);

    const TapeTime low_tone = 1000 + 8 * 256;
    rig.run_to(low_tone + 2 * 512);
    CHECK_FALSE(rig.ula.cassette_rxd());
    const uint64_t before = rig.ula.rx_clocks();
    rig.run_to(low_tone + 6 * 512);  // two 1200 Hz cycles of two edges: two bits
    CHECK(rig.ula.rx_clocks() - before == 32);
}

TEST_CASE("Separator times out only on gaps longer than 350 clocks", "[serial_ula][cassette]") {
    // A gap of 340 clocks is short: its edge's burst and nothing more. One of
    // 384 clocks (312 us, the intermediate gap a 90 degree shift produces) is
    // past the 350-clock timeout, so it gains a timeout burst and reads long.
    struct Case { uint32_t spacing; uint64_t clocks; };
    for (const Case c : {Case{340, 4}, Case{384, 8}}) {
        CAPTURE(c.spacing);
        Rig rig;
        rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
        std::vector<TapeItem> items;
        TapeTime t = 1000;
        add_edges(items, t, c.spacing, 6);
        rig.load_items(items);
        rig.run_to(1000 + c.spacing);       // just before the second edge
        const uint64_t before = rig.ula.rx_clocks();
        rig.run_to(1000 + 2 * c.spacing);   // the whole gap after the second edge
        CHECK(rig.ula.rx_clocks() - before == c.clocks);
    }
}

TEST_CASE("Separator ends a burst at an edge and starts a fresh one", "[serial_ula][cassette]") {
    Rig rig;
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.load_items({TapeItem::edge(1000, true), TapeItem::edge(1006, false), TapeItem::edge(5000, true)});
    rig.run_to(1006);
    CHECK(rig.ula.rx_clocks() == 2);  // pulses at 1000 and 1004
    rig.run_to(1006 + SerialUla::BURST_CLOCKS + 10);
    CHECK(rig.ula.rx_clocks() == 2 + 4);
}

// =============================================================================
// 7. Bytes through the separator into the ACIA
// =============================================================================

namespace {

bool receive_from_tape(uint16_t tape_baud, uint8_t acia_divide, uint8_t value) {
    TapeBuilder builder;
    const std::vector<uint8_t> bytes{value};
    if (tape_baud == 300) builder.baud(300);
    builder.carrier(200).bytes_8n1(bytes).carrier(200);
    Rig rig;
    rig.acia.write_control(acia_8n1(acia_divide));
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.load_uef(builder);
    // The first edges after silence recover a 0, so the ACIA may frame a byte
    // of nothing as the leader starts, as on the machine; read past it.
    for (int i = 0; i < 20'000'000 && rig.deck.transport() == CassetteDeck::Transport::Playing; ++i) {
        rig.ula.tick();
        if (rig.acia.rdrf()) {
            const bool clean = (rig.acia.read_status() & (Mc6850::SR_FE | Mc6850::SR_PE)) == 0;
            if (rig.acia.read_data() == value && clean) return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("A tape byte arrives intact at the matching ACIA divider only", "[serial_ula][cassette]") {
    CHECK(receive_from_tape(1200, Mc6850::COUNTER_DIVIDE_16, 0x5A));
    CHECK(receive_from_tape(1200, Mc6850::COUNTER_DIVIDE_16, 0x2A));
    CHECK(receive_from_tape(300, Mc6850::COUNTER_DIVIDE_64, 0x5A));
    CHECK_FALSE(receive_from_tape(300, Mc6850::COUNTER_DIVIDE_16, 0x5A));
    CHECK_FALSE(receive_from_tape(1200, Mc6850::COUNTER_DIVIDE_64, 0x5A));
}

// =============================================================================
// 8. The high-tone detector
// =============================================================================

namespace {

// Carrier from 1000 for `cycles` 2400 Hz cycles of 512 clocks.
std::vector<TapeItem> carrier_items(uint32_t cycles, TapeTime start = 1000) {
    std::vector<TapeItem> items;
    TapeTime t = start;
    add_edges(items, t, 256, cycles * 2);
    return items;
}

// Run until the detector's /DCD output rises; returns the clock it rose on,
// or 0 if it did not by `limit`.
uint64_t run_until_dcd(Rig& rig, uint64_t limit) {
    while (rig.ula.ula_clocks() < limit) {
        rig.ula.tick();
        if (rig.ula.dcd_output()) return rig.ula.ula_clocks();
    }
    return 0;
}

// The clock RxD first reads 1 once the tape's edges have begun (before any
// edge it idles high).
uint64_t run_until_rxd_one(Rig& rig) {
    while (rig.ula.cassette_rxd() || rig.deck.position() <= 1000) rig.ula.tick();
    while (!rig.ula.cassette_rxd()) rig.ula.tick();
    return rig.ula.ula_clocks();
}

}  // namespace

TEST_CASE("Detector pulses /DCD for one cell after 962 cells of high tone on a Ferranti part",
          "[serial_ula][cassette]") {
    Rig rig;
    rig.acia.write_control(acia_8n1(Mc6850::COUNTER_DIVIDE_16) | Mc6850::CR_RX_IRQ_ENABLE);
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.load_items(carrier_items(2000));

    const uint64_t rxd_one = run_until_rxd_one(rig);
    // 961 cells of carrier give no /DCD.
    CHECK(run_until_dcd(rig, rxd_one + 960 * 256) == 0);
    const uint64_t rose = run_until_dcd(rig, rxd_one + 964 * 256);
    REQUIRE(rose != 0);
    const uint64_t cells = (rose - rxd_one) / 256;
    CHECK(cells >= 961);
    CHECK(cells <= 963);

    // The ACIA latches it and raises the interrupt.
    CHECK((rig.acia.status() & Mc6850::SR_NOT_DCD) != 0);
    CHECK(rig.acia.irq_pending());

    // One cell long.
    while (rig.ula.dcd_output()) rig.ula.tick();
    // ULA clocks per tick vary (0 or 1), so count in ULA clocks.
    CHECK(rig.ula.ula_clocks() - rose == SerialUla::DETECTOR_CELL_CLOCKS);

    // Carrier continuing gives no second pulse.
    CHECK(run_until_dcd(rig, rig.ula.ula_clocks() + 900 * 256) == 0);
}

TEST_CASE("Detector re-arms on a 0 bit and on motor off", "[serial_ula][cassette]") {
    SECTION("a 0 bit then 962 cells") {
        Rig rig;
        rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
        auto items = carrier_items(1100);
        TapeTime t = items.back().time + 256;
        add_edges(items, t, 512, 2);   // one 1200 Hz cycle: a 0
        add_edges(items, t, 256, 2200);
        rig.load_items(items);
        REQUIRE(run_until_dcd(rig, 1000 + 1100 * 512) != 0);
        rig.run_to(t - 2200 * 256 + 1);  // past the 0
        CHECK(rig.ula.high_tone_cells() < 5);
        CHECK(run_until_dcd(rig, rig.ula.ula_clocks() + 970 * 256) != 0);
    }
    SECTION("motor off then on then 962 cells") {
        Rig rig;
        rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
        rig.load_items(carrier_items(3000));
        REQUIRE(run_until_dcd(rig, 1000 + 1100 * 512) != 0);
        rig.ula.write(0, ULA_CASSETTE_MOTOR_OFF);
        CHECK(rig.ula.high_tone_cells() == 0);
        rig.run(1000);
        rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
        CHECK(run_until_dcd(rig, rig.ula.ula_clocks() + 970 * 256) != 0);
    }
}

TEST_CASE("Detector is re-armed by a silence", "[serial_ula][cassette]") {
    // The first edge after a silence ends a timed-out gap, so it recovers a 0
    // and clears the count: 500 cells, silence, then 462 more give no pulse,
    // and 962 more do.
    Rig rig;
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    auto items = carrier_items(250);                 // 500 cells
    TapeTime t = items.back().time + 50'000;         // silence
    const TapeTime resume = t;
    add_edges(items, t, 256, 2400);
    rig.load_items(items);
    rig.run_to(resume);
    CHECK(rig.ula.high_tone_cells() > 400);
    rig.run_to(resume + 3 * 256);
    CHECK(rig.ula.high_tone_cells() < 3);
    CHECK(run_until_dcd(rig, resume + 462 * 256) == 0);
    CHECK(run_until_dcd(rig, resume + 966 * 256) != 0);
}

TEST_CASE("Detector count survives latch writes that keep the motor on", "[serial_ula][cassette]") {
    Rig rig;
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.load_items(carrier_items(2000));
    run_until_rxd_one(rig);
    rig.run(500 * 256);
    const uint32_t counted = rig.ula.high_tone_cells();
    REQUIRE(counted > 400);

    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);                                // the same value
    CHECK(rig.ula.high_tone_cells() == counted);
    rig.ula.write(0, SerialUla::MOTOR_ENABLE | SerialUla::RS423_SELECT);    // RS423, motor on
    CHECK_FALSE(rig.acia.not_dcd());                                         // forced low
    rig.run(10 * 256);
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);                                // back to cassette
    CHECK(rig.ula.high_tone_cells() >= counted);
}

TEST_CASE("Detector threshold is 445 cells on a VLSI part", "[serial_ula][cassette]") {
    Rig rig(SerialUla::Part::VLSI);
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.load_items(carrier_items(1000));
    const uint64_t rxd_one = run_until_rxd_one(rig);
    const uint64_t rose = run_until_dcd(rig, rxd_one + 450 * 256);
    REQUIRE(rose != 0);
    const uint64_t cells = (rose - rxd_one) / 256;
    CHECK(cells >= 444);
    CHECK(cells <= 446);
}

TEST_CASE("VLSI part with latch bit 3 inverts the tone sense", "[serial_ula][cassette]") {
    Rig rig(SerialUla::Part::VLSI);
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON | SerialUla::VLSI_TONE_INVERT);
    rig.load_items(carrier_items(20));
    rig.run_to(1000 + 8 * 256);
    CHECK_FALSE(rig.ula.cassette_rxd());  // high tone reads as 0

    Rig ferranti;
    ferranti.ula.write(0, ULA_CASSETTE_MOTOR_ON | SerialUla::VLSI_TONE_INVERT);
    ferranti.load_items(carrier_items(20));
    ferranti.run_to(1000 + 8 * 256);
    CHECK(ferranti.ula.cassette_rxd());   // the Ferranti ignores bit 3
}

// =============================================================================
// 9. The tone synthesiser
// =============================================================================

TEST_CASE("Tone synthesiser sends 2400 Hz while idle and 1200 Hz for space", "[serial_ula][cassette]") {
    Rig rig;
    RecordingSink sink;
    rig.ula.set_tape_sink(&sink);
    rig.acia.write_control(acia_8n1(Mc6850::COUNTER_DIVIDE_16));  // RTS low
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.run(4 * 1024);
    REQUIRE(sink.edges.size() >= 8);
    for (size_t i = 1; i < sink.edges.size(); ++i) CHECK(sink.edges[i] - sink.edges[i - 1] == 256);

    // A transmitted &00: the start bit and eight zeros are space cells.
    sink.edges.clear();
    rig.acia.write_data(0x00);
    rig.run(14 * 1024);
    size_t space_halves = 0;
    for (size_t i = 1; i < sink.edges.size(); ++i) {
        if (sink.edges[i] - sink.edges[i - 1] == 512) ++space_halves;
    }
    // Nine cells of one 1200 Hz cycle: 18 half-cycles, give or take the
    // boundaries of the run.
    CHECK(space_halves >= 17);
    CHECK(space_halves <= 19);
}

TEST_CASE("Tone synthesiser is silent with RTS high or RS423 selected", "[serial_ula][cassette]") {
    Rig rig;
    RecordingSink sink;
    rig.ula.set_tape_sink(&sink);
    rig.acia.write_control(acia_8n1(Mc6850::COUNTER_DIVIDE_16)
                           | (Mc6850::TX_CTRL_RTS_HIGH_NO_IRQ << Mc6850::CR_TX_CONTROL_SHIFT));
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.run(4 * 1024);
    CHECK(sink.edges.empty());
    CHECK(sink.silences > 0);

    rig.acia.write_control(acia_8n1(Mc6850::COUNTER_DIVIDE_16));
    rig.ula.write(0, SerialUla::RS423_SELECT);
    rig.run(4 * 1024);
    CHECK(sink.edges.empty());
}

TEST_CASE("Cassette TDRE returns after ten bits with no tape sink", "[serial_ula][cassette]") {
    Rig rig;
    rig.acia.write_control(acia_8n1(Mc6850::COUNTER_DIVIDE_16));
    rig.ula.write(0, ULA_CASSETTE_MOTOR_ON);
    rig.acia.write_data(0x41);
    while (!rig.acia.tdre()) rig.ula.tick();
    rig.acia.write_data(0x42);
    const uint64_t start = rig.ula.ula_clocks();
    while (!rig.acia.tdre()) rig.ula.tick();
    const uint64_t elapsed = rig.ula.ula_clocks() - start;
    CHECK(elapsed + 1 >= 10 * 1024);
    CHECK(elapsed <= 10 * 1024 + 1);
}

// =============================================================================
// 10. The deck
// =============================================================================

TEST_CASE("Deck moves only when playing with the relay closed", "[cassette]") {
    TapeBuilder builder;
    builder.carrier(1000);
    CassetteDeck deck;
    const auto bytes = builder.uef();
    deck.insert(UefTape::open(bytes, "tone.uef"), "tone.uef");
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);

    for (int i = 0; i < 100; ++i) deck.clock(true);
    CHECK(deck.position() == 0);   // stopped

    deck.play();
    for (int i = 0; i < 100; ++i) deck.clock(false);
    CHECK(deck.position() == 0);   // relay open

    for (int i = 0; i < 100; ++i) deck.clock(true);
    CHECK(deck.position() == 100);

    deck.stop();
    for (int i = 0; i < 100; ++i) deck.clock(true);
    CHECK(deck.position() == 100);

    deck.seek(50'000);
    CHECK(deck.position() == 50'000);
    deck.seek(0);
    CHECK(deck.position() == 0);

    // The end stops the deck.
    deck.play();
    const TapeTime length = *deck.duration();
    for (TapeTime i = 0; i < length + 10; ++i) deck.clock(true);
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
    CHECK(deck.position() == length);
}

namespace {

// A tape of carrier lasting about `seconds`.
std::unique_ptr<TapeSignal> carrier_tape(double seconds) {
    TapeBuilder builder;
    builder.carrier(static_cast<uint32_t>(seconds * 2400));
    const auto bytes = builder.uef();
    return UefTape::open(bytes, "tone.uef");
}

TapeTime clocks_of(double seconds) { return static_cast<TapeTime>(seconds * ULA_CLOCK_HZ); }

// Clock the deck `clocks` times with the relay as given; returns the edges.
int run_deck(CassetteDeck& deck, TapeTime clocks, bool relay_closed) {
    int edges = 0;
    for (TapeTime i = 0; i < clocks; ++i) edges += deck.clock(relay_closed) ? 1 : 0;
    return edges;
}

}  // namespace

TEST_CASE("Deck rewinds from 60 s to the start in 2 s and stops", "[cassette]") {
    CassetteDeck deck;
    deck.insert(carrier_tape(90), "tone.uef");
    const TapeTime from = clocks_of(60);
    deck.seek(from);
    deck.rewind();
    CHECK(deck.transport() == CassetteDeck::Transport::Rewinding);

    const TapeTime needed = (from + CassetteDeck::WIND_SPEED - 1) / CassetteDeck::WIND_SPEED;
    CHECK(run_deck(deck, needed - 1, true) == 0);
    CHECK(deck.transport() == CassetteDeck::Transport::Rewinding);
    CHECK(deck.position() > 0);
    CHECK(run_deck(deck, 1, true) == 0);
    CHECK(deck.position() == 0);
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
    // 2 s of emulated time, to the clock.
    CHECK(static_cast<double>(needed) / ULA_CLOCK_HZ == Catch::Approx(2.0).epsilon(1e-6));
}

TEST_CASE("Deck fast-forwards a 90 s tape to its end in 3 s and stops", "[cassette]") {
    CassetteDeck deck;
    deck.insert(carrier_tape(90), "tone.uef");
    const TapeTime end = *deck.duration();
    CHECK(static_cast<double>(end) / ULA_CLOCK_HZ == Catch::Approx(90.0).epsilon(0.01));
    deck.fast_forward();
    CHECK(deck.transport() == CassetteDeck::Transport::FastForwarding);

    const TapeTime needed = (end + CassetteDeck::WIND_SPEED - 1) / CassetteDeck::WIND_SPEED;
    CHECK(run_deck(deck, needed, false) == 0);   // the relay open does not stop a wind
    CHECK(deck.position() == end);
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
    CHECK(static_cast<double>(needed) / ULA_CLOCK_HZ == Catch::Approx(3.0).epsilon(0.01));
}

TEST_CASE("Deck stop mid-wind holds and play mid-wind plays from there", "[cassette]") {
    CassetteDeck deck;
    deck.insert(carrier_tape(90), "tone.uef");
    deck.fast_forward();
    CHECK(run_deck(deck, 1000, true) == 0);
    deck.stop();
    const TapeTime held = deck.position();
    CHECK(held == 1000 * CassetteDeck::WIND_SPEED);
    CHECK(run_deck(deck, 1000, true) == 0);
    CHECK(deck.position() == held);

    deck.rewind();
    CHECK(run_deck(deck, 500, true) == 0);
    deck.play();
    const TapeTime from = deck.position();
    CHECK(from == held - 500 * CassetteDeck::WIND_SPEED);
    CHECK(deck.transport() == CassetteDeck::Transport::Playing);
    CHECK(run_deck(deck, 1024, true) > 0);   // the tape is on the head again
    CHECK(deck.position() == from + 1024);
}

TEST_CASE("Deck seek ends a wind, and a wind at its end stops at once", "[cassette]") {
    CassetteDeck deck;
    deck.insert(carrier_tape(10), "tone.uef");
    deck.rewind();   // already at the start
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);

    deck.fast_forward();
    run_deck(deck, 100, true);
    deck.seek(clocks_of(1));
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
    CHECK(deck.position() == clocks_of(1));

    deck.seek(*deck.duration());
    deck.fast_forward();
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
}

TEST_CASE("Deck refuses a second tape with a sentence", "[cassette]") {
    TapeBuilder builder;
    builder.carrier(10);
    const auto bytes = builder.uef();
    CassetteDeck deck;
    deck.insert(UefTape::open(bytes, "one.uef"), "one.uef");
    CHECK_THROWS_WITH(deck.insert(UefTape::open(bytes, "two.uef"), "two.uef"),
                      "The deck already holds one.uef; eject it before inserting another tape.");
    deck.eject();
    CHECK_FALSE(deck.has_tape());
    CHECK_NOTHROW(deck.insert(UefTape::open(bytes, "two.uef"), "two.uef"));
}

TEST_CASE("Break and power cycle leave the deck alone", "[cassette]") {
    ModelB machine;
    auto& deck = machine.memory().cassette_deck;
    TapeBuilder builder;
    builder.carrier(1000);
    const auto bytes = builder.uef();
    deck.insert(UefTape::open(bytes, "tone.uef"), "tone.uef");
    deck.play();
    deck.seek(12'345);

    machine.reset();
    CHECK(deck.position() == 12'345);
    CHECK(deck.transport() == CassetteDeck::Transport::Playing);
    machine.memory().soft_reset();
    CHECK(deck.position() == 12'345);
    CHECK(deck.transport() == CassetteDeck::Transport::Playing);
}

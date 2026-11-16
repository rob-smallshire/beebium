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

// The tape signal seam and its two readers, UEF and CSW, against tapes the
// test builds itself (cassette round 4, #208).

#include "support/tape_builder.hpp"

#include <beebium/cassette/CswTape.hpp>
#include <beebium/cassette/TapeImage.hpp>
#include <beebium/cassette/UefTape.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace beebium;
using beebium::test::CswOptions;
using beebium::test::TapeBuilder;

namespace {

// ULA clocks in a half-cycle at `hz`.
double half(double hz) { return ULA_CLOCK_HZ / (2.0 * hz); }

std::vector<TapeItem> all_items(TapeSignal& tape) {
    std::vector<TapeItem> items;
    for (;;) {
        items.push_back(tape.next());
        if (items.back().type == TapeItem::Type::End) return items;
        REQUIRE(items.size() < 10'000'000);
    }
}

std::vector<TapeItem> edges_of(const std::vector<TapeItem>& items) {
    std::vector<TapeItem> edges;
    for (const auto& item : items) {
        if (item.type == TapeItem::Type::Edge) edges.push_back(item);
    }
    return edges;
}

std::unique_ptr<UefTape> uef_of(const TapeBuilder& builder) {
    const auto bytes = builder.uef();
    return UefTape::open(bytes, "test.uef");
}

// Classify the spacing between consecutive edges as short (a 2400 Hz half-cycle)
// or long (a 1200 Hz one), in order.
std::string half_cycle_pattern(const std::vector<TapeItem>& edges, size_t from, size_t count) {
    std::string pattern;
    for (size_t i = from; i < from + count && i + 1 < edges.size(); ++i) {
        const double gap = static_cast<double>(edges[i + 1].time - edges[i].time);
        pattern.push_back(gap > 1.5 * half(2400) ? 'L' : 'S');
    }
    return pattern;
}

// Decode a run of edges, in 1200 encoding, back into bits: a long half-cycle
// pair is a 0, two short pairs a 1.
std::vector<int> decode_bits(const std::vector<TapeItem>& edges, size_t from, size_t bits) {
    std::vector<int> out;
    size_t i = from;
    while (out.size() < bits && i + 1 < edges.size()) {
        const double gap = static_cast<double>(edges[i + 1].time - edges[i].time);
        if (gap > 1.5 * half(2400)) {
            out.push_back(0);
            i += 2;
        } else {
            out.push_back(1);
            i += 4;
        }
    }
    return out;
}

}  // namespace

// =============================================================================
// 1. The CRC
// =============================================================================

TEST_CASE("Tape builder CRC-16/XMODEM gives the check value 0x31C3", "[uef]") {
    const std::string check = "123456789";
    const std::vector<uint8_t> bytes(check.begin(), check.end());
    CHECK(beebium::test::crc16_xmodem(bytes) == 0x31C3);
}

// =============================================================================
// 2. UEF rendering
// =============================================================================

TEST_CASE("UEF carrier yields two edges a cycle a 2400 Hz half-cycle apart", "[uef]") {
    TapeBuilder builder;
    builder.carrier(100);
    auto tape = uef_of(builder);
    const auto edges = edges_of(all_items(*tape));
    REQUIRE(edges.size() == 200);
    for (size_t i = 1; i < edges.size(); ++i) {
        // 256.4 clocks: 256 or 257 as the fraction carries.
        const auto spacing = edges[i].time - edges[i - 1].time;
        CHECK(spacing >= 256);
        CHECK(spacing <= 257);
        CHECK(edges[i].level != edges[i - 1].level);
    }
    CHECK(edges.back().time == static_cast<TapeTime>(199 * half(2400)));
}

TEST_CASE("UEF 0100 byte edges fall where the 1200 encoding puts them", "[uef]") {
    const std::vector<uint8_t> byte{0x55};
    TapeBuilder builder;
    builder.bytes_8n1(byte);
    auto tape = uef_of(builder);
    const auto edges = edges_of(all_items(*tape));
    // Frame 0 1010 1010 1: start, data LSB first, stop. A 0 is one 1200 Hz
    // cycle (two edges), a 1 two 2400 Hz cycles (four edges).
    const int bits[10] = {0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
    std::vector<double> expected;
    double t = 0.0;
    for (int b : bits) {
        const int halves = b ? 4 : 2;
        for (int h = 0; h < halves; ++h) {
            expected.push_back(t);
            t += half(b ? 2400 : 1200);
        }
    }
    REQUIRE(edges.size() == expected.size());
    for (size_t i = 0; i < edges.size(); ++i) {
        CAPTURE(i);
        CHECK(edges[i].time == static_cast<TapeTime>(expected[i]));
    }
}

TEST_CASE("UEF 0117 selects the 300 encoding of four and eight cycles a bit", "[uef]") {
    const std::vector<uint8_t> byte{0x00};
    TapeBuilder builder;
    builder.baud(300).bytes_8n1(byte);
    auto tape = uef_of(builder);
    const auto edges = edges_of(all_items(*tape));
    // Start and eight zeros: 9 bits of 4 long cycles; the stop bit 8 short.
    REQUIRE(edges.size() == 9 * 8 + 16);
    CHECK(half_cycle_pattern(edges, 0, 72) == std::string(72, 'L'));
    CHECK(half_cycle_pattern(edges, 72, 15) == std::string(15, 'S'));
}

TEST_CASE("UEF integer and floating-point gaps are silence of the right length", "[uef]") {
    std::vector<uint8_t> integer_gap{0x60, 0x09};  // 2400 units of 1/2400 s: 1 s
    TapeBuilder builder;
    builder.carrier(1).raw_chunk(0x0112, integer_gap).carrier(1).gap(0.5).carrier(1);
    auto tape = uef_of(builder);
    const auto items = all_items(*tape);
    std::vector<TapeItem> gaps;
    for (const auto& item : items) {
        if (item.type == TapeItem::Type::Gap) gaps.push_back(item);
    }
    REQUIRE(gaps.size() == 2);
    const double first_gap_end = 2 * half(2400) + ULA_CLOCK_HZ;
    CHECK(gaps[0].time == static_cast<TapeTime>(first_gap_end));
    CHECK(gaps[1].time == static_cast<TapeTime>(first_gap_end + 2 * half(2400) + 0.5 * ULA_CLOCK_HZ));
}

TEST_CASE("UEF 0113 changes the base frequency and with it the spacing", "[uef]") {
    float hz = 1000.0f;
    std::vector<uint8_t> body(4);
    std::memcpy(body.data(), &hz, 4);
    TapeBuilder builder;
    builder.raw_chunk(0x0113, body).carrier(10);
    auto tape = uef_of(builder);
    const auto edges = edges_of(all_items(*tape));
    REQUIRE(edges.size() == 20);
    const auto spacing = static_cast<double>(edges[11].time - edges[10].time);
    CHECK(std::abs(spacing - half(2000)) <= 1.0);
}

TEST_CASE("UEF 0113 outside 100 to 10000 Hz is refused", "[uef]") {
    float hz = 50.0f;
    std::vector<uint8_t> body(4);
    std::memcpy(body.data(), &hz, 4);
    TapeBuilder builder;
    builder.raw_chunk(0x0113, body);
    const auto bytes = builder.uef();
    CHECK_THROWS_WITH(UefTape::open(bytes, "fast.uef"),
                      "The file fast.uef changes the base frequency to 50 Hz, which is outside 100 to 10000 Hz.");
}

TEST_CASE("UEF 0115 phase sets the first level after a gap", "[uef]") {
    TapeBuilder default_phase;
    default_phase.gap(0.2).carrier(4);
    auto tape = uef_of(default_phase);
    auto edges = edges_of(all_items(*tape));
    REQUIRE_FALSE(edges.empty());
    CHECK(edges.front().level);  // 180 degrees: the cycle begins high

    TapeBuilder zero_phase;
    zero_phase.raw_chunk(0x0115, {0, 0}).gap(0.2).carrier(4);
    tape = uef_of(zero_phase);
    edges = edges_of(all_items(*tape));
    REQUIRE_FALSE(edges.empty());
    CHECK_FALSE(edges.front().level);  // 0 degrees: the cycle begins low
}

TEST_CASE("UEF 0111 is carrier, the dummy byte AA, then carrier", "[uef]") {
    TapeBuilder dummy;
    dummy.raw_chunk(0x0111, {10, 0, 20, 0});
    const std::vector<uint8_t> aa{0xAA};
    TapeBuilder reference;
    reference.carrier(10).bytes_8n1(aa).carrier(20);

    auto a = uef_of(dummy);
    auto b = uef_of(reference);
    const auto ea = edges_of(all_items(*a));
    const auto eb = edges_of(all_items(*b));
    REQUIRE(ea.size() == eb.size());
    for (size_t i = 0; i < ea.size(); ++i) CHECK(ea[i].time == eb[i].time);
}

TEST_CASE("UEF 0104 frames 7E1 with an even parity bit, swapped for old MakeUEF", "[uef]") {
    // 0x45 = 100 0101: three ones, so even parity sends 1 and odd sends 0.
    const std::vector<uint8_t> body{7, 'E', 1, 0x45};
    auto frame_bits = [&](const std::string& origin) {
        TapeBuilder builder;
        if (!origin.empty()) builder.origin(origin);
        builder.raw_chunk(0x0104, body);
        auto tape = uef_of(builder);
        const auto edges = edges_of(all_items(*tape));
        return decode_bits(edges, 0, 10);
    };
    const std::vector<int> even = {0, 1, 0, 1, 0, 0, 0, 1, 1, 1};
    const std::vector<int> odd = {0, 1, 0, 1, 0, 0, 0, 1, 0, 1};
    CHECK(frame_bits("") == even);
    CHECK(frame_bits("MakeUEF V2.4") == even);
    CHECK(frame_bits("MakeUEF V2.1") == odd);
}

TEST_CASE("UEF 0104 with other than 7 or 8 data bits is refused", "[uef]") {
    TapeBuilder builder;
    builder.raw_chunk(0x0104, {6, 'N', 1, 0x00});
    const auto bytes = builder.uef();
    CHECK_THROWS_WITH(UefTape::open(bytes, "six.uef"),
                      "The file six.uef has a chunk &0104 with 6 data bits a packet, which Beebium does not read.");
}

TEST_CASE("UEF 0114 security cycles decode least significant bit first, 1 long",
          "[uef]") {
    // The specification's worked example: LSLLLSSLSSLLSL stored as 9D 2C.
    TapeBuilder builder;
    builder.raw_chunk(0x0114, {14, 0, 0, 'W', 'W', 0x9D, 0x2C});
    auto tape = uef_of(builder);
    const auto edges = edges_of(all_items(*tape));
    REQUIRE(edges.size() == 28);
    // Each cycle is two equal halves; read every other spacing.
    std::string cycles;
    for (size_t i = 0; i + 1 < edges.size(); i += 2) {
        const double spacing = static_cast<double>(edges[i + 1].time - edges[i].time);
        cycles.push_back(spacing > 1.5 * half(2400) ? 'L' : 'S');
    }
    CHECK(cycles == "LSLLLSSLSSLLSL");
}

TEST_CASE("UEF 0117 other than 300 or 1200 is refused", "[uef]") {
    TapeBuilder builder;
    builder.raw_chunk(0x0117, {0x58, 0x02});  // 600
    const auto bytes = builder.uef();
    CHECK_THROWS_WITH(UefTape::open(bytes, "odd.uef"),
                      "The file odd.uef selects a data encoding of 600 baud, which is neither 300 nor 1200.");
}

TEST_CASE("UEF keeps origin and title metadata", "[uef]") {
    std::vector<uint8_t> title{'D', 'e', 'm', 'o', 0};
    TapeBuilder builder;
    builder.origin("tests").raw_chunk(0x0009, title).raw_chunk(0x0005, {0x20});
    auto tape = uef_of(builder);
    CHECK(tape->metadata().origin == "tests");
    CHECK(tape->metadata().title == "Demo");
    CHECK(tape->metadata().target_machine == "BBC Model B");
}

// =============================================================================
// 3. gzip, refusals and unknown chunks
// =============================================================================

TEST_CASE("UEF gzipped reads the same as plain", "[uef]") {
    const std::vector<uint8_t> data{1, 2, 3, 4, 5};
    TapeBuilder builder;
    builder.file("FILE", 0x3000, 0x3000, data, {});
    const auto plain = builder.uef(false);
    const auto gzipped = builder.uef(true);
    REQUIRE(gzipped[0] == 0x1F);
    auto a = open_tape_image(plain, "plain.uef");
    auto b = open_tape_image(gzipped, "gzipped.uef");
    const auto ia = all_items(*a);
    const auto ib = all_items(*b);
    REQUIRE(ia.size() == ib.size());
    for (size_t i = 0; i < ia.size(); ++i) {
        CHECK(ia[i].time == ib[i].time);
        CHECK(ia[i].type == ib[i].type);
    }
}

TEST_CASE("UEF disc and machine-state chunks are refused with a sentence", "[uef]") {
    for (const uint16_t id : {uint16_t{0x0200}, uint16_t{0x0400}}) {
        TapeBuilder builder;
        builder.carrier(1).raw_chunk(id, {0});
        const auto bytes = builder.uef();
        char hex[8];
        std::snprintf(hex, sizeof(hex), "&%04X", id);
        CHECK_THROWS_WITH(UefTape::open(bytes, "state.uef"),
                          std::string("The file state.uef contains chunk ") + hex +
                          ", which holds a disc, a ROM or a machine's state rather than tape, "
                          "so Beebium does not read it.");
    }
}

TEST_CASE("UEF unknown chunks are skipped", "[uef]") {
    TapeBuilder builder;
    builder.carrier(2).raw_chunk(0x0150, {1, 2, 3}).carrier(2);
    auto tape = uef_of(builder);
    CHECK(edges_of(all_items(*tape)).size() == 8);
}

TEST_CASE("UEF of a major version other than 0 is refused", "[uef]") {
    std::vector<uint8_t> bytes = {'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', 0, 2, 1};
    CHECK_THROWS_WITH(UefTape::open(bytes, "new.uef"),
                      "The file new.uef is a UEF of version 1.2, which Beebium does not read.");
}

TEST_CASE("A file that is neither UEF nor CSW is refused", "[uef][csw]") {
    const std::vector<uint8_t> bytes = {'h', 'e', 'l', 'l', 'o'};
    CHECK_THROWS_WITH(open_tape_image(bytes, "notes.txt"),
                      "The file notes.txt is not a UEF or CSW tape image.");
}

// =============================================================================
// 4. Seeking and duration
// =============================================================================

TEST_CASE("UEF seek to the middle of a carrier lands within one cycle", "[uef]") {
    const std::vector<uint8_t> data(300, 0x41);
    TapeBuilder builder;
    builder.file("FILE", 0x3000, 0x3000, data, {});
    auto tape = uef_of(builder);
    const TapeTime target = seconds_to_tape_time(2.0);  // inside the 5.1 s leader
    tape->seek(target);
    const TapeItem item = tape->next();
    REQUIRE(item.type == TapeItem::Type::Edge);
    CHECK(item.time >= target);
    CHECK(item.time - target <= static_cast<TapeTime>(2 * half(2400)) + 1);

    tape->rewind();
    CHECK(tape->next().time == 0);
}

TEST_CASE("UEF duration is the end of the last half-cycle", "[uef]") {
    TapeBuilder builder;
    builder.carrier(50);
    auto tape = uef_of(builder);
    const auto items = all_items(*tape);
    const auto edges = edges_of(items);
    REQUIRE(tape->duration().has_value());
    CHECK(*tape->duration() == items.back().time);
    CHECK(*tape->duration() > edges.back().time);
    CHECK(*tape->duration() - edges.back().time <= static_cast<TapeTime>(half(2400)) + 1);
}

// =============================================================================
// 5. CSW
// =============================================================================

TEST_CASE("CSW of a tape gives the UEF's edge times within one clock", "[csw]") {
    const std::vector<uint8_t> data(300, 0x5A);
    TapeBuilder builder;
    builder.file("FILE", 0x3000, 0x3000, data, {});
    builder.gap(0.25).carrier(100);
    auto uef = uef_of(builder);
    const auto csw_bytes = builder.csw();
    auto csw = CswTape::open(csw_bytes, "test.csw");

    const auto ue = all_items(*uef);
    const auto ce = all_items(*csw);
    REQUIRE(ue.size() == ce.size());
    for (size_t i = 0; i < ue.size(); ++i) {
        CAPTURE(i);
        REQUIRE(ue[i].type == ce[i].type);
        const auto diff = ue[i].time > ce[i].time ? ue[i].time - ce[i].time : ce[i].time - ue[i].time;
        CHECK(diff <= 1);
    }
}

TEST_CASE("CSW 300 encoding matches the UEF too", "[csw]") {
    const std::vector<uint8_t> data(40, 0xC3);
    TapeBuilder builder;
    builder.file("SLOW", 0x3000, 0x3000, data, {.baud = 300});
    auto uef = uef_of(builder);
    auto csw = CswTape::open(builder.csw(), "slow.csw");
    const auto ue = edges_of(all_items(*uef));
    const auto ce = edges_of(all_items(*csw));
    REQUIRE(ue.size() == ce.size());
    for (size_t i = 0; i < ue.size(); i += 97) {
        const auto diff = ue[i].time > ce[i].time ? ue[i].time - ce[i].time : ce[i].time - ue[i].time;
        CHECK(diff <= 1);
    }
}

TEST_CASE("CSW version 1.01 and version 2 RLE headers read", "[csw]") {
    TapeBuilder builder;
    builder.carrier(20);
    for (const CswOptions options : {CswOptions{.version = 1, .z_rle = false},
                                     CswOptions{.version = 2, .z_rle = false},
                                     CswOptions{.version = 2, .z_rle = true}}) {
        CAPTURE(options.version, options.z_rle);
        auto tape = CswTape::open(builder.csw(options), "tone.csw");
        CHECK(tape->sample_rate() == 48000);
        CHECK(edges_of(all_items(*tape)).size() == 40);
    }
}

TEST_CASE("CSW polarity flag flips the first level", "[csw]") {
    TapeBuilder builder;
    builder.carrier(2);
    auto high = CswTape::open(builder.csw({.initial_high = true}), "high.csw");
    auto low = CswTape::open(builder.csw({.initial_high = false}), "low.csw");
    CHECK(high->next().level);
    CHECK_FALSE(low->next().level);
}

TEST_CASE("CSW pulse longer than 100 ms is a gap", "[csw]") {
    TapeBuilder builder;
    builder.carrier(2).gap(0.2).carrier(2);
    auto tape = CswTape::open(builder.csw(), "gap.csw");
    const auto items = all_items(*tape);
    size_t gaps = 0;
    for (const auto& item : items) gaps += item.type == TapeItem::Type::Gap ? 1 : 0;
    CHECK(gaps == 1);
    CHECK(edges_of(items).size() == 8);
}

TEST_CASE("CSW with a sample rate of 0 is refused", "[csw]") {
    TapeBuilder builder;
    builder.carrier(2);
    auto bytes = builder.csw({.z_rle = false});
    std::memset(&bytes[0x19], 0, 4);
    CHECK_THROWS_WITH(CswTape::open(bytes, "zero.csw"),
                      "The file zero.csw gives a sample rate of 0, so it cannot be played.");
}

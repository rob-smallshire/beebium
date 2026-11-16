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

// Recording on a tape: the editable UEF, the classifier and writer, the deck's
// Recording transport and its write-back (#208, cassette round 6).

#include "support/tape_builder.hpp"
#include "test_temp_paths.hpp"

#include <beebium/cassette/CassetteDeck.hpp>
#include <beebium/cassette/CswTape.hpp>
#include <beebium/cassette/EdgeTrack.hpp>
#include <beebium/cassette/TapeImage.hpp>
#include <beebium/cassette/UefTape.hpp>
#include <beebium/cassette/UefWriter.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace beebium;
using beebium::test::ScopedTempDir;
using beebium::test::TapeBuilder;

namespace {

constexpr double SHORT_HALF = ULA_CLOCK_HZ / 4800.0;
constexpr double LONG_HALF = ULA_CLOCK_HZ / 2400.0;

std::vector<UefChunk> parse_uef(std::span<const uint8_t> bytes) {
    std::vector<UefChunk> chunks;
    REQUIRE(bytes.size() >= 12);
    REQUIRE(std::memcmp(bytes.data(), "UEF File!", 10) == 0);
    size_t pos = 12;
    while (pos + 6 <= bytes.size()) {
        UefChunk chunk;
        chunk.id = static_cast<uint16_t>(bytes[pos] | (bytes[pos + 1] << 8));
        const uint32_t length = bytes[pos + 2] | (bytes[pos + 3] << 8) | (bytes[pos + 4] << 16) |
                                (static_cast<uint32_t>(bytes[pos + 5]) << 24);
        pos += 6;
        chunk.body.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                          bytes.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += length;
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

std::vector<uint8_t> u16(uint16_t v) { return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)}; }

// A tape's edges, as an EdgeTrack of its length.
EdgeTrack track_of(TapeSignal& tape) {
    EdgeTrack track(*tape.duration());
    tape.rewind();
    for (TapeItem item = tape.next(); item.type != TapeItem::Type::End; item = tape.next()) {
        if (item.type == TapeItem::Type::Edge) track.append(item.time, item.level);
    }
    return track;
}

std::vector<UefChunk> classify(const std::vector<uint8_t>& uef) {
    auto tape = UefTape::open(uef, "test.uef");
    return classify_tape_edges(track_of(*tape));
}

float f32(const std::vector<uint8_t>& body) {
    float f;
    std::memcpy(&f, body.data(), 4);
    return f;
}

// What a tape holds, as the classifier sees it: each silence's length, and
// between them each half-cycle's class (true short, false long). The last
// half-cycle of a run of edges has no length of its own and is left out.
struct Signature {
    std::vector<double> gaps;
    std::vector<std::vector<bool>> regions;
};

Signature signature_of(TapeSignal& tape) {
    Signature s;
    tape.rewind();
    std::optional<TapeTime> last;
    double silence_from = 0.0;
    for (TapeItem item = tape.next(); item.type != TapeItem::Type::End; item = tape.next()) {
        if (item.type != TapeItem::Type::Edge) continue;
        if (!last || item.time - *last > 1024) {
            if (last) silence_from = static_cast<double>(*last) + (s.regions.back().empty() || s.regions.back().back()
                                                                       ? SHORT_HALF : LONG_HALF);
            s.gaps.push_back(static_cast<double>(item.time) - silence_from);
            s.regions.emplace_back();
        } else {
            const double d = static_cast<double>(item.time - *last);
            s.regions.back().push_back(std::fabs(d - SHORT_HALF) < std::fabs(d - LONG_HALF));
        }
        last = item.time;
    }
    return s;
}

void check_same_content(TapeSignal& original, TapeSignal& written) {
    const Signature a = signature_of(original);
    const Signature b = signature_of(written);
    REQUIRE(a.regions.size() == b.regions.size());
    for (size_t i = 0; i < a.regions.size(); ++i) {
        CHECK(a.regions[i] == b.regions[i]);
        CHECK(std::fabs(a.gaps[i] - b.gaps[i]) <= LONG_HALF);
    }
}

std::vector<uint8_t> read_file(const std::filesystem::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void write_file(const std::filesystem::path& filepath, std::span<const uint8_t> bytes) {
    std::ofstream out(filepath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// What the Serial ULA's synthesiser sends, as edges at exact 256 and 512 clock
// half-cycles: carrier, then bytes as 8N1 frames at 1200 baud, from `start`.
class Synthesiser {
public:
    explicit Synthesiser(TapeTime start) : time_(start) {}
    void carrier(size_t cycles) { for (size_t i = 0; i < cycles; ++i) cycle(256); }
    void bit(bool one) {
        if (one) { cycle(256); cycle(256); }
        else cycle(512);
    }
    void byte(uint8_t value) {
        bit(false);
        for (int i = 0; i < 8; ++i) bit(((value >> i) & 1) != 0);
        bit(true);
    }
    void silence(TapeTime clocks) { time_ += clocks; }
    const std::vector<EdgeTrack::Edge>& edges() const { return edges_; }
    TapeTime time() const { return time_; }

private:
    void cycle(TapeTime half) {
        for (int i = 0; i < 2; ++i) {
            level_ = !level_;
            edges_.push_back({time_, level_});
            time_ += half;
        }
    }
    TapeTime time_;
    bool level_ = false;
    std::vector<EdgeTrack::Edge> edges_;
};

// A deck with a tape and the clocks to drive it.
void run(CassetteDeck& deck, TapeTime clocks, bool relay_closed = true) {
    for (TapeTime i = 0; i < clocks; ++i) deck.clock(relay_closed);
}

// Record `edges` through the deck as the ULA would deliver them: one clock at
// a time, each edge on its clock.
void record_through(CassetteDeck& deck, const std::vector<EdgeTrack::Edge>& edges, TapeTime until) {
    size_t next = 0;
    while (deck.position() < until && deck.recording()) {
        const TapeTime now = deck.position();
        deck.clock(true);
        while (next < edges.size() && edges[next].time < now) ++next;
        if (next < edges.size() && edges[next].time == now) deck.edge(now, edges[next++].level);
    }
}

}  // namespace

// =============================================================================
// 1. The classifier
// =============================================================================

TEST_CASE("Classifier writes carrier of 2n half-cycles as one 0110 of n", "[uef][recording]") {
    TapeBuilder builder;
    builder.carrier(1000);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].id == 0x0110);
    CHECK(chunks[0].body == u16(1000));
}

TEST_CASE("Classifier splits 70000 cycles of carrier into two 0110 chunks", "[uef][recording]") {
    TapeBuilder builder;
    builder.carrier(70000);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 2);
    CHECK(chunks[0].body == u16(65535));
    CHECK(chunks[1].body == u16(70000 - 65535));
}

TEST_CASE("Classifier writes a 900 s silence as an 0116 of 900.0", "[uef][recording]") {
    TapeBuilder builder;
    builder.gap(900.0);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].id == 0x0116);
    CHECK(f32(chunks[0].body) == 900.0f);
}

TEST_CASE("Classifier writes twelve 1200 Hz half-cycles of silence as an 0112 of 12", "[uef][recording]") {
    TapeBuilder builder;
    builder.carrier(10).raw_chunk(0x0112, u16(12)).carrier(10);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 3);
    CHECK(chunks[0].id == 0x0110);
    CHECK(chunks[1].id == 0x0112);
    CHECK(chunks[1].body == u16(12));
    CHECK(chunks[2].id == 0x0110);
}

TEST_CASE("Classifier writes an 8N1 byte as 0100", "[uef][recording]") {
    TapeBuilder builder;
    const uint8_t byte[] = {0x2A};
    builder.carrier(50).bytes_8n1(byte).carrier(50);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 3);
    CHECK(chunks[1].id == 0x0100);
    CHECK(chunks[1].body == std::vector<uint8_t>{0x2A});
}

TEST_CASE("Classifier writes a 300 baud byte as 0117 of 300 then 0100", "[uef][recording]") {
    TapeBuilder builder;
    const uint8_t byte[] = {0x2A};
    builder.carrier(50).baud(300).bytes_8n1(byte).carrier(50);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 4);
    CHECK(chunks[0].id == 0x0110);
    CHECK(chunks[1].id == 0x0117);
    CHECK(chunks[1].body == u16(300));
    CHECK(chunks[2].id == 0x0100);
    CHECK(chunks[2].body == std::vector<uint8_t>{0x2A});
    CHECK(chunks[3].id == 0x0110);
}

TEST_CASE("Classifier writes an 8E1 byte that 8N1 cannot frame as 0104", "[uef][recording]") {
    // 0x03 has an even parity bit of 0, where 8N1 needs its stop bit.
    TapeBuilder builder;
    builder.carrier(50).raw_chunk(0x0104, {8, 'E', 1, 0x03}).carrier(50);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 3);
    CHECK(chunks[1].id == 0x0104);
    CHECK(chunks[1].body == std::vector<uint8_t>{8, 'E', 1, 0x03});
}

TEST_CASE("Classifier writes a 7E1 byte as the 8N1 byte it is cycle for cycle", "[uef][recording]") {
    // Start, seven data bits, parity, stop: ten bits, as 8N1's start, eight
    // data bits and stop are, so the cycles are an 8N1 byte with the parity
    // bit as bit 7.
    TapeBuilder builder;
    builder.carrier(50).raw_chunk(0x0104, {7, 'E', 1, 0x34}).carrier(50);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 3);
    CHECK(chunks[1].id == 0x0100);
    CHECK(chunks[1].body == std::vector<uint8_t>{0xB4});
}

TEST_CASE("Classifier writes bits that frame as nothing as 0102", "[uef][recording]") {
    // Eleven 0 bits, a 1, two 0s, a 1: no stop bit where any format, at
    // either baud, needs one.
    const std::vector<bool> bits = {false, false, false, false, false, false, false, false, false, false,
                                    false, true, false, false, true};
    std::vector<uint8_t> body(1 + 2, 0);
    body[0] = static_cast<uint8_t>(16 - bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) body[1 + i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    }
    TapeBuilder builder;
    builder.raw_chunk(0x0102, body);
    const auto chunks = classify(builder.uef());
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].id == 0x0102);
    CHECK(chunks[0].body == body);
}

TEST_CASE("Writer puts the metadata chunks first, unchanged", "[uef][recording]") {
    TapeBuilder builder;
    builder.origin("Some tool 1.0").raw_chunk(0x0001, {'L', 'o', 'a', 'd', 0}).carrier(100)
        .raw_chunk(0x0005, {0x20}).carrier(100);
    auto tape = UefTape::open(builder.uef(), "meta.uef");
    const auto chunks = parse_uef(tape->recorder()->encode());
    REQUIRE(chunks.size() >= 4);
    CHECK(chunks[0].id == 0x0000);
    CHECK(chunks[0].body == std::vector<uint8_t>{'S', 'o', 'm', 'e', ' ', 't', 'o', 'o', 'l', ' ', '1', '.', '0', 0});
    CHECK(chunks[1].id == 0x0001);
    CHECK(chunks[2].id == 0x0005);
    CHECK(chunks[3].id == 0x0110);
    CHECK(chunks[3].body == u16(200));
}

// =============================================================================
// 2. Round trip
// =============================================================================

TEST_CASE("A tape written back reads as the same cycles and gaps, and a file", "[uef][recording]") {
    const std::vector<uint8_t> data = {1, 2, 3, 0x2A, 0xFF, 0x00};
    std::vector<uint8_t> explicit_bits = {3, 0x00, 0b00010100};  // thirteen bits
    TapeBuilder builder;
    builder.origin("tests").gap(0.5).file("TEST", 0x1900, 0x8023, data, {})
        .raw_chunk(0x0102, explicit_bits).gap(2.0).carrier(500).gap(1.25);

    for (const bool gzip : {false, true}) {
        const auto original_bytes = builder.uef(gzip);
        auto original = UefTape::open(original_bytes, "trip.uef");
        auto copy = UefTape::open(original_bytes, "trip.uef");
        const auto written_bytes = copy->recorder()->encode();
        CHECK((written_bytes.size() > 2 && written_bytes[0] == 0x1F && written_bytes[1] == 0x8B) == gzip);
        auto written = UefTape::open(written_bytes, "trip.uef");
        check_same_content(*original, *written);
        CHECK(std::fabs(static_cast<double>(*written->duration()) - static_cast<double>(*original->duration())) <=
              LONG_HALF);
    }
}

TEST_CASE("A recording written back reads as its cycles, its times stretched by the nominal base",
          "[uef][recording]") {
    // What the synthesiser sends: carrier, bytes, an unframed stretch, carrier.
    Synthesiser synth(0);
    synth.carrier(2000);
    for (uint8_t b : {0x2A, 0x41, 0x00, 0xFF}) synth.byte(b);
    for (int i = 0; i < 13; ++i) synth.bit(false);
    synth.bit(true);
    synth.carrier(300);
    const TapeTime length = synth.time() + 100'000;

    EdgeTrack track(length);
    for (const auto& e : synth.edges()) track.append(e.time, e.level);
    const auto chunks = classify_tape_edges(track);
    bool has_0102 = false;
    for (const auto& c : chunks) has_0102 = has_0102 || c.id == 0x0102;
    CHECK(has_0102);

    auto written = UefTape::open(encode_uef({}, chunks, false), "recorded.uef");
    // Every edge comes back, each at its time stretched by 1200 / (16 MHz / 13 / 1024).
    std::vector<TapeTime> back;
    for (TapeItem item = written->next(); item.type != TapeItem::Type::End; item = written->next()) {
        if (item.type == TapeItem::Type::Edge) back.push_back(item.time);
    }
    REQUIRE(back.size() == synth.edges().size());
    const double ratio = 1200.0 / (ULA_CLOCK_HZ / 1024.0);
    for (size_t i = 0; i < back.size(); ++i) {
        CHECK(std::fabs(static_cast<double>(back[i]) * ratio - static_cast<double>(synth.edges()[i].time)) <= 1.0);
    }
}

// =============================================================================
// 3. Overwriting
// =============================================================================

TEST_CASE("Recording silence over a carrier tape from 20 s to 30 s leaves carrier either side",
          "[uef][recording]") {
    TapeBuilder builder;
    builder.carrier(60 * 2400);
    CassetteDeck deck;
    deck.insert(UefTape::open(builder.uef(), "carrier.uef"), "carrier.uef");
    const TapeTime from = seconds_to_tape_time(20.0);
    const TapeTime to = seconds_to_tape_time(30.0);
    deck.seek(from);
    deck.record();
    CHECK(deck.transport() == CassetteDeck::Transport::Recording);
    run(deck, to - from);   // the machine sends nothing
    CHECK(deck.position() == to);
    deck.stop();
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);

    deck.seek(0);
    deck.play();
    std::vector<TapeTime> edges;
    for (TapeTime i = 0; i < seconds_to_tape_time(40.0); ++i) {
        const TapeTime now = deck.position();
        if (deck.clock(true)) edges.push_back(now);
    }
    REQUIRE_FALSE(edges.empty());
    size_t before = 0, inside = 0, after = 0;
    for (TapeTime t : edges) {
        if (t < from) ++before;
        else if (t < to) ++inside;
        else ++after;
    }
    CHECK(before > 0);
    CHECK(inside == 0);
    CHECK(after > 0);
}

TEST_CASE("Recording reaches the end of the tape and stops there", "[uef][recording]") {
    TapeBuilder builder;
    builder.carrier(2400);
    CassetteDeck deck;
    deck.insert(UefTape::open(builder.uef(), "short.uef"), "short.uef");
    const TapeTime end = *deck.duration();
    deck.seek(end - 1000);
    deck.record();
    run(deck, 2000);
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
    CHECK(deck.position() == end);
}

TEST_CASE("A recording through the deck reads back as what was sent", "[uef][recording]") {
    TapeBuilder builder;
    builder.gap(30.0);
    CassetteDeck deck;
    deck.insert(UefTape::open(builder.uef(), "blank.uef"), "blank.uef");
    const TapeTime from = seconds_to_tape_time(1.0);
    deck.seek(from);
    deck.record();
    Synthesiser synth(from);
    synth.carrier(500);
    synth.byte(0x2A);
    synth.carrier(500);
    record_through(deck, synth.edges(), synth.time() + 10'000);
    deck.stop();

    deck.seek(0);
    deck.play();
    std::vector<TapeTime> back;
    for (TapeTime i = 0; i < seconds_to_tape_time(3.0); ++i) {
        const TapeTime now = deck.position();
        if (deck.clock(true)) back.push_back(now);
    }
    REQUIRE(back.size() == synth.edges().size());
    for (size_t i = 0; i < back.size(); ++i) CHECK(back[i] == synth.edges()[i].time);
}

TEST_CASE("Recording ends on play, a wind and a seek, and nothing is lost when the relay opens",
          "[uef][recording]") {
    TapeBuilder builder;
    builder.gap(10.0);
    CassetteDeck deck;
    deck.insert(UefTape::open(builder.uef(), "blank.uef"), "blank.uef");
    deck.record();
    run(deck, 1000, false);
    CHECK(deck.position() == 0);   // the relay is open: the tape does not move
    CHECK(deck.transport() == CassetteDeck::Transport::Recording);
    run(deck, 1000);
    CHECK(deck.position() == 1000);
    deck.play();
    CHECK(deck.transport() == CassetteDeck::Transport::Playing);
    deck.record();
    deck.fast_forward();
    CHECK(deck.transport() == CassetteDeck::Transport::FastForwarding);
    deck.record();
    CHECK(deck.transport() == CassetteDeck::Transport::Recording);
    deck.seek(5000);
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
}

// =============================================================================
// 4. Writing back
// =============================================================================

TEST_CASE("A tape never recorded on is not rewritten on eject", "[uef][recording]") {
    ScopedTempDir dir("beebium_tape_recording_");
    const auto filepath = dir.path() / "untouched.uef";
    TapeBuilder builder;
    builder.carrier(2400);
    write_file(filepath, builder.uef());
    const auto bytes = read_file(filepath);
    const auto mtime = std::filesystem::last_write_time(filepath);

    CassetteDeck deck;
    deck.insert(open_tape_file(filepath), "untouched.uef", "", filepath);
    deck.play();
    run(deck, 10'000);
    deck.eject();
    CHECK(read_file(filepath) == bytes);
    CHECK((std::filesystem::last_write_time(filepath) == mtime));
}

TEST_CASE("A tape recorded on is rewritten on stop, on eject and when the deck goes", "[uef][recording]") {
    ScopedTempDir dir("beebium_tape_recording_");
    const auto filepath = dir.path() / "recorded.uef";
    TapeBuilder builder;
    builder.carrier(10 * 2400);
    write_file(filepath, builder.uef());

    auto silence_written_at = [&](TapeTime from, TapeTime to) {
        auto tape = open_tape_file(filepath);
        tape->seek(from);
        const TapeItem item = tape->next();
        return item.type == TapeItem::Type::End || item.time >= to;
    };
    const TapeTime one = seconds_to_tape_time(1.0);
    const TapeTime two = seconds_to_tape_time(2.0);

    SECTION("stop") {
        CassetteDeck deck;
        deck.insert(open_tape_file(filepath), "recorded.uef", "", filepath);
        deck.seek(one);
        deck.record();
        run(deck, one);
        deck.stop();
        CHECK(silence_written_at(one + 1024, two - 1024));
    }
    SECTION("eject") {
        CassetteDeck deck;
        deck.insert(open_tape_file(filepath), "recorded.uef", "", filepath);
        deck.seek(one);
        deck.record();
        run(deck, one);
        deck.eject();
        CHECK(silence_written_at(one + 1024, two - 1024));
    }
    SECTION("the deck's end") {
        {
            CassetteDeck deck;
            deck.insert(open_tape_file(filepath), "recorded.uef", "", filepath);
            deck.seek(one);
            deck.record();
            run(deck, one);
        }
        CHECK(silence_written_at(one + 1024, two - 1024));
    }
    SECTION("flush, recording on") {
        CassetteDeck deck;
        deck.insert(open_tape_file(filepath), "recorded.uef", "", filepath);
        deck.seek(one);
        deck.record();
        run(deck, one);
        CHECK(deck.flush());
        CHECK(deck.transport() == CassetteDeck::Transport::Recording);
        CHECK(silence_written_at(one + 1024, two - 1024));
    }
}

#ifndef _WIN32
TEST_CASE("A tape that cannot be written back is left whole and the failure said", "[uef][recording]") {
    ScopedTempDir dir("beebium_tape_recording_");
    const auto subdirpath = dir.path() / "locked";
    std::filesystem::create_directory(subdirpath);
    const auto filepath = subdirpath / "locked.uef";
    TapeBuilder builder;
    builder.carrier(4 * 2400);
    write_file(filepath, builder.uef());
    const auto bytes = read_file(filepath);

    std::ostringstream captured;
    auto* old_buffer = std::cerr.rdbuf(captured.rdbuf());
    {
        CassetteDeck deck;
        deck.insert(open_tape_file(filepath), "locked.uef", "", filepath);
        deck.seek(seconds_to_tape_time(1.0));
        deck.record();
        run(deck, seconds_to_tape_time(1.0));
        std::filesystem::permissions(subdirpath, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
        deck.stop();
        CHECK_FALSE(deck.flush());
        size_t entries = 0;
        for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(subdirpath)) ++entries;
        CHECK(entries == 1);   // no partial file beside it
        CHECK(read_file(filepath) == bytes);
        std::filesystem::permissions(subdirpath, std::filesystem::perms::owner_all);
        deck.eject();   // writable again: the recording is kept
    }
    std::cerr.rdbuf(old_buffer);

    CHECK_THAT(captured.str(), Catch::Matchers::ContainsSubstring(
        "The tape locked.uef could not be written back to"));
    CHECK(read_file(filepath) != bytes);
}
#endif

// =============================================================================
// 5. Tapes that cannot be recorded on
// =============================================================================

TEST_CASE("Record on a CSW or an empty deck is refused with a sentence", "[uef][recording]") {
    CassetteDeck deck;
    CHECK_THROWS_WITH(deck.record(), "There is no tape in the deck.");

    TapeBuilder builder;
    builder.carrier(2400);
    deck.insert(CswTape::open(builder.csw(), "game.csw"), "game.csw");
    CHECK_THROWS_WITH(deck.record(), "The tape game.csw is a CSW image, and Beebium records only on UEF tapes.");
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
}

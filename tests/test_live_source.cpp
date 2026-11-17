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

// Live audio: the ring between a stream and the emulation thread, the live
// tape and the deck's rules for it (#208, cassette round 8).

#include <beebium/cassette/CassetteDeck.hpp>
#include <beebium/cassette/LiveSource.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <cmath>
#include <cstring>
#include <numbers>
#include <thread>
#include <vector>

using namespace beebium;

namespace {

constexpr uint32_t RATE = 48000;
constexpr size_t CHUNK = 480;  // 10 ms

// Samples [from, from + count) of a sine at `hz`, amplitude 0.5, at 90 degrees
// (a cosine), as a tape's playback gives.
std::vector<float> tone(double hz, uint64_t from, size_t count) {
    std::vector<float> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i] = static_cast<float>(0.5 * std::cos(2.0 * std::numbers::pi * hz * static_cast<double>(from + i) / RATE));
    }
    return out;
}

// Take `samples` samples from a live tape, one at a time as emulated time
// reaches each, and return the edges' times.
std::vector<TapeTime> consume(LiveTape& tape, size_t samples) {
    std::vector<TapeTime> edges;
    size_t taken = 0;
    while (taken < samples) {
        const TapeItem item = tape.next();
        if (item.type == TapeItem::Type::Edge) {
            edges.push_back(item.time);
        }
        ++taken;  // each next() takes exactly one sample
    }
    return edges;
}

}  // namespace

// =============================================================================
// 1. The ring
// =============================================================================

TEST_CASE("Live source paced one sample a period gives a tone's edges and no runs", "[cassette][live]") {
    auto source = std::make_shared<LiveSource>(RATE);
    LiveTape tape(source, "left");
    tape.next();  // the look ahead a deck takes on insert, which costs no sample
    std::vector<TapeTime> edges;
    for (uint64_t chunk = 0; chunk < 100; ++chunk) {  // one second
        source->push(tone(2400.0, chunk * CHUNK, CHUNK));
        const auto more = consume(tape, CHUNK);
        edges.insert(edges.end(), more.begin(), more.end());
    }
    CHECK(source->underruns() == 0);
    CHECK(source->overruns() == 0);
    CHECK(source->received() == 100 * CHUNK);
    CHECK(source->consumed() == 100 * CHUNK);
    // A quarter of a second priming, then three quarters of tone.
    REQUIRE(edges.size() > 3500);
    for (size_t i = edges.size() / 2; i + 1 < edges.size(); ++i) {
        CHECK(std::llabs(static_cast<long long>(edges[i + 1] - edges[i]) - 256) <= 2);
    }
}

TEST_CASE("Live source primes a quarter of a second before it plays, and again after starving", "[cassette][live]") {
    auto source = std::make_shared<LiveSource>(RATE);
    LiveTape tape(source, "left");
    tape.next();
    CHECK(source->prime_level() == RATE / 4);
    // A stream that starts late: silence while nothing comes, and no underrun.
    CHECK(consume(tape, RATE / 2).empty());
    CHECK(source->underruns() == 0);
    CHECK(source->priming());
    // Less than the priming level: still waiting, still nothing counted.
    source->push(tone(2400.0, 0, RATE / 5));
    CHECK(consume(tape, RATE / 10).empty());
    CHECK(source->priming());
    CHECK(source->underruns() == 0);
    // Past it: the tone plays, everything that came, and then the ring starves.
    source->push(tone(2400.0, RATE / 5, RATE / 10));
    const auto edges = consume(tape, RATE * 3 / 10);
    CHECK_FALSE(edges.empty());
    CHECK(source->underruns() == 0);
    // Starved: silence, counted, until the ring has primed again.
    consume(tape, RATE / 10);
    CHECK(source->underruns() == RATE / 10);
    CHECK(source->priming());
    source->push(tone(2400.0, 0, RATE / 10));  // not enough to prime
    consume(tape, RATE / 10);
    CHECK(source->underruns() == 2 * RATE / 10);
    source->push(tone(2400.0, 0, RATE / 5));   // now a quarter of a second and more
    consume(tape, 10);
    CHECK_FALSE(source->priming());
    CHECK(source->underruns() == 2 * RATE / 10);
}

TEST_CASE("Live source plays out an ended stream without priming and counts no underrun", "[cassette][live]") {
    auto source = std::make_shared<LiveSource>(RATE);
    LiveTape tape(source, "left");
    tape.next();
    source->push(tone(2400.0, 0, RATE / 10));   // less than the priming level
    source->end_stream();
    const auto edges = consume(tape, RATE / 5);
    CHECK_FALSE(edges.empty());                  // heard, though it never primed
    CHECK(source->empty());
    CHECK(source->underruns() == 0);             // an empty ring is the end of the tape
}

TEST_CASE("Live source gives silence and counts it when the stream stops", "[cassette][live]") {
    auto source = std::make_shared<LiveSource>(RATE);
    LiveTape tape(source, "left");
    tape.next();  // the look ahead a deck takes on insert, which costs no sample
    source->push(tone(2400.0, 0, RATE / 2));  // half a second
    const auto edges = consume(tape, RATE);    // a second taken
    CHECK(source->underruns() == RATE / 2);
    CHECK(source->overruns() == 0);
    // Nothing after the stream stopped, but the filters' ringing.
    for (TapeTime t : edges) CHECK(t < seconds_to_tape_time(0.5) + 2000);
}

TEST_CASE("Live source too fast fills the ring and drops at the excess rate", "[cassette][live]") {
    auto source = std::make_shared<LiveSource>(RATE);
    LiveTape tape(source, "left");
    tape.next();  // the look ahead a deck takes on insert, which costs no sample
    // 10% faster than emulated time takes it: 528 samples a 10 ms chunk.
    constexpr size_t FAST = CHUNK * 11 / 10;
    uint64_t produced = 0;
    for (int chunk = 0; chunk < 3000; ++chunk) {  // 30 s: the ring fills after 20 s
        source->push(tone(2400.0, produced, FAST));
        produced += FAST;
        consume(tape, CHUNK);
    }
    CHECK(source->fill() > 0.95);
    // Full by about chunk 2000; each chunk after drops the excess of 48.
    const uint64_t overruns_at_30 = source->overruns();
    for (int chunk = 0; chunk < 100; ++chunk) {
        source->push(tone(2400.0, produced, FAST));
        produced += FAST;
        consume(tape, CHUNK);
    }
    CHECK(source->overruns() - overruns_at_30 == 100 * (FAST - CHUNK));
    CHECK(source->underruns() == 0);
}

TEST_CASE("Live source drops the oldest samples and keeps the newest", "[cassette][live]") {
    LiveSource source(RATE);
    const size_t capacity = source.capacity();
    std::vector<float> ramp(capacity + 1000);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<float>(i) / static_cast<float>(ramp.size());
    // Two pushes, together 1000 more than the ring holds.
    source.push(std::span<const float>(ramp).first(capacity));
    source.push(std::span<const float>(ramp).subspan(capacity));
    CHECK(source.overruns() == 1000);
    // The first sample read is the 1000th pushed; the last, the last.
    CHECK(source.next_sample() == ramp[1000]);
    float last = 0.0f;
    for (size_t i = 1; i < capacity; ++i) last = source.next_sample();
    CHECK(last == ramp.back());
    CHECK(source.underruns() == 0);
    source.next_sample();
    CHECK(source.underruns() == 1);
}

TEST_CASE("Live source ring holds up with a producer and a consumer on two threads", "[cassette][live]") {
    // A producer racing ahead of a consumer: every sample read is one pushed,
    // in order, and every sample is either read, dropped or still in the ring.
    LiveSource source(8000);
    constexpr uint64_t TOTAL = 400000;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        std::vector<float> chunk(97);
        for (uint64_t n = 0; n < TOTAL; n += chunk.size()) {
            for (size_t i = 0; i < chunk.size(); ++i) chunk[i] = static_cast<float>(n + i);
            source.push(chunk);
        }
        done = true;
    });
    float previous = -1.0f;
    uint64_t read = 0;
    bool ordered = true;
    while (!done || source.fill() > 0.0) {
        const uint64_t underruns = source.underruns();
        const bool priming = source.priming();
        const float v = source.next_sample();
        if (source.underruns() != underruns || (priming && source.priming())) continue;
        ordered = ordered && v > previous;
        previous = v;
        ++read;
    }
    producer.join();
    CHECK(ordered);
    CHECK(read + source.overruns() == source.received());
}

// =============================================================================
// 2. Formats and channels
// =============================================================================

namespace {

std::vector<uint8_t> encode(const std::vector<float>& mono, AudioDecoder::Format format, unsigned channels,
                            unsigned signal_channel) {
    std::vector<uint8_t> out;
    for (float s : mono) {
        for (unsigned c = 0; c < channels; ++c) {
            const float v = c == signal_channel ? s : 0.0f;
            if (format == AudioDecoder::Format::S16) {
                const auto i = static_cast<int16_t>(std::lround(v * 32767.0f));
                out.push_back(static_cast<uint8_t>(i));
                out.push_back(static_cast<uint8_t>(i >> 8));
            } else {
                uint8_t b[4];
                std::memcpy(b, &v, 4);
                out.insert(out.end(), b, b + 4);
            }
        }
    }
    return out;
}

// Edges of a second of carrier delivered as `bytes`, in chunks of an awkward
// size so frames split between chunks.
std::vector<TapeTime> edges_of_bytes(const std::vector<uint8_t>& bytes, AudioDecoder::Format format,
                                     unsigned channels, unsigned channel) {
    auto source = std::make_shared<LiveSource>(RATE);
    LiveTape tape(source, channel == 0 ? "left" : "right");
    tape.next();
    AudioDecoder decoder(format, channels, channel);
    // Decode it all, a frame split here and there, then take it: the ring
    // primes at the same sample whatever the format.
    std::vector<float> samples;
    size_t total = 0;
    for (size_t at = 0; at < bytes.size(); at += 1001) {
        samples.clear();
        decoder.decode(std::span<const uint8_t>(bytes).subspan(at, std::min<size_t>(1001, bytes.size() - at)), samples);
        source->push(samples);
        total += samples.size();
    }
    return consume(tape, total);
}

}  // namespace

TEST_CASE("Live audio in S16 and F32, mono and either stereo channel, gives the same edges", "[cassette][live]") {
    const auto mono = tone(2400.0, 0, RATE);
    const auto reference = edges_of_bytes(encode(mono, AudioDecoder::Format::F32, 1, 0), AudioDecoder::Format::F32, 1, 0);
    REQUIRE(reference.size() > 3500);
    struct Case { AudioDecoder::Format format; unsigned channels; unsigned channel; };
    for (const Case c : {Case{AudioDecoder::Format::S16, 1, 0}, Case{AudioDecoder::Format::S16, 2, 0},
                         Case{AudioDecoder::Format::S16, 2, 1}, Case{AudioDecoder::Format::F32, 2, 0},
                         Case{AudioDecoder::Format::F32, 2, 1}}) {
        CAPTURE(static_cast<int>(c.format), c.channels, c.channel);
        const auto edges = edges_of_bytes(encode(mono, c.format, c.channels, c.channel), c.format, c.channels, c.channel);
        REQUIRE(edges.size() == reference.size());
        for (size_t i = 0; i < edges.size(); ++i) {
            CHECK(std::llabs(static_cast<long long>(edges[i] - reference[i])) <= 1);
        }
    }
}

// =============================================================================
// The deck with a live tape
// =============================================================================

TEST_CASE("Deck plays a live tape whatever the relay, paced by emulated time", "[cassette][live]") {
    auto source = std::make_shared<LiveSource>(RATE);
    CassetteDeck deck;
    deck.insert(std::make_unique<LiveTape>(source, "left"), "Line in");
    CHECK(deck.live());
    CHECK(deck.transport() == CassetteDeck::Transport::Stopped);
    CHECK_FALSE(deck.duration().has_value());

    source->push(tone(2400.0, 0, RATE));  // a second
    // Stopped: the samples are taken and go nowhere.
    size_t edges = 0;
    for (TapeTime i = 0; i < seconds_to_tape_time(0.25); ++i) edges += deck.clock(false) ? 1 : 0;
    CHECK(edges == 0);
    CHECK(source->consumed() > RATE / 4 - 2);
    CHECK(source->consumed() < RATE / 4 + 2);
    // Playing with the relay open: the edges come through.
    deck.play();
    for (TapeTime i = 0; i < seconds_to_tape_time(0.25); ++i) edges += deck.clock(false) ? 1 : 0;
    CHECK(edges > 1100);   // 4800 edges a second
    CHECK(edges < 1300);
    CHECK(source->underruns() == 0);
    CHECK(deck.position() > seconds_to_tape_time(0.5) - 100);

    // What a live tape cannot do does nothing.
    const TapeTime at = deck.position();
    deck.seek(0);
    deck.rewind();
    deck.fast_forward();
    CHECK(deck.position() == at);
    CHECK(deck.transport() == CassetteDeck::Transport::Playing);
    CHECK_FALSE(deck.recordable());
    CHECK_THROWS_WITH(deck.record(), "The live input Line in cannot be recorded on.");

    // Ejecting lets the stream know.
    CHECK_FALSE(source->closed());
    deck.eject();
    CHECK(source->closed());
}

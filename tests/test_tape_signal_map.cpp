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

// The tape signal map (#208, round 9): where a tape carries signal, a level a
// second, computed from the TapeSignal the deck would play.

#include "support/tape_builder.hpp"
#include "test_temp_paths.hpp"

#include <beebium/cassette/CassetteDeck.hpp>
#include <beebium/cassette/LiveSource.hpp>
#include <beebium/cassette/SignalMap.hpp>
#include <beebium/cassette/TapeImage.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using beebium::compute_signal_map;
using beebium::open_tape_image;
using beebium::test::TapeBuilder;
using beebium::test::WavOptions;

namespace {

std::vector<uint8_t> filler(size_t count) {
    std::vector<uint8_t> bytes(count);
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(i * 37 + 11);
    return bytes;
}

// 2.5 s of gap, 3 s of carrier, a 2 s block of bytes, 1 s of gap, then 1 s of
// carrier and a second 2 s block: 11.5 s, so twelve seconds of map.
TapeBuilder two_blocks() {
    const auto bytes = filler(240);  // 240 8N1 frames at 1200 baud: 2 s
    TapeBuilder builder;
    builder.gap(2.5).carrier(7200).bytes_8n1(bytes).gap(1.0).carrier(2400).bytes_8n1(bytes);
    return builder;
}

std::vector<uint8_t> map_of(const std::vector<uint8_t>& image, const std::string& name) {
    auto tape = open_tape_image(image, name);
    const auto levels = compute_signal_map(*tape);
    REQUIRE(levels);
    return *levels;
}

// What two_blocks() maps to: silent, half a second each side of a boundary,
// and signal throughout.
void check_two_blocks(const std::vector<uint8_t>& levels, int tolerance) {
    REQUIRE(levels.size() == 12);
    const int expected[12] = {0, 0, 128, 255, 255, 255, 255, 128, 128, 255, 255, 255};
    for (size_t s = 0; s < 12; ++s) {
        CAPTURE(s, static_cast<int>(levels[s]));
        CHECK(std::abs(static_cast<int>(levels[s]) - expected[s]) <= tolerance);
    }
}

void check_matches(const std::vector<uint8_t>& levels, const std::vector<uint8_t>& reference, int tolerance) {
    REQUIRE(levels.size() == reference.size());
    for (size_t s = 0; s < levels.size(); ++s) {
        CAPTURE(s, static_cast<int>(levels[s]), static_cast<int>(reference[s]));
        CHECK(std::abs(static_cast<int>(levels[s]) - static_cast<int>(reference[s])) <= tolerance);
    }
}

}  // namespace

TEST_CASE("A UEF maps silent seconds to 0, signal to 255 and boundaries between", "[cassette][signal_map]") {
    const auto levels = map_of(two_blocks().uef(), "two_blocks.uef");
    // The last second is the half second the tape holds, all of it signal.
    check_two_blocks(levels, 10);
    CHECK(levels[0] == 0);
    CHECK(levels[1] == 0);
    CHECK(levels[4] == 255);
    CHECK(levels[5] == 255);
}

TEST_CASE("A 300 baud block is signal throughout", "[cassette][signal_map]") {
    TapeBuilder builder;
    builder.carrier(2400).baud(300).bytes_8n1(filler(90));  // 1 s of carrier, then 3 s of data
    const auto levels = map_of(builder.uef(), "slow.uef");
    REQUIRE(levels.size() == 4);
    for (size_t s = 0; s < levels.size(); ++s) {
        CAPTURE(s, static_cast<int>(levels[s]));
        CHECK(levels[s] >= 250);
    }
}

TEST_CASE("A WAV of a UEF maps as the UEF does", "[cassette][signal_map][wav]") {
    const auto builder = two_blocks();
    const auto reference = map_of(builder.uef(), "two_blocks.uef");
    WavOptions wav;
    wav.amplitude = 0.5;
    wav.shape = WavOptions::Shape::Shifted90;

    SECTION("clean") {
        check_matches(map_of(builder.wav(wav), "two_blocks.wav"), reference, 16);
    }
    SECTION("with noise at 0.01 throughout, the gaps still silent") {
        wav.noise = 0.01;
        check_matches(map_of(builder.wav(wav), "noisy.wav"), reference, 16);
    }
    SECTION("with 50 Hz hum at 0.5 in the gaps, still silent") {
        wav.hum = 0.5;
        check_matches(map_of(builder.wav(wav), "hum.wav"), reference, 16);
    }
}

TEST_CASE("A CSW maps as the UEF it was made from", "[cassette][signal_map]") {
    const auto builder = two_blocks();
    check_matches(map_of(builder.csw(), "two_blocks.csw"), map_of(builder.uef(), "two_blocks.uef"), 2);
}

// =============================================================================
// The deck's mapper: off the emulation thread, latest job wins
// =============================================================================

namespace {

template <typename Done>
beebium::SignalMapper::Map wait_for(const beebium::SignalMapper& mapper, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto map = mapper.snapshot();
    while (!done(map) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        map = mapper.snapshot();
    }
    return map;
}

// A WAV of silence long enough that walking it takes a good while.
std::shared_ptr<const std::vector<uint8_t>> long_silent_wav() {
    TapeBuilder builder;
    builder.gap(600.0).carrier(2400);
    WavOptions wav;
    wav.rate = 22050;
    return std::make_shared<const std::vector<uint8_t>>(builder.wav(wav));
}

}  // namespace

TEST_CASE("A superseded signal map job never publishes", "[cassette][signal_map]") {
    const auto slow = long_silent_wav();
    const auto quick = std::make_shared<const std::vector<uint8_t>>(two_blocks().uef());
    beebium::SignalMapper mapper;
    mapper.map([slow] { return open_tape_image(*slow, "slow.wav"); });
    mapper.map([quick] { return open_tape_image(*quick, "quick.uef"); });
    const auto map = wait_for(mapper, [](const auto& m) { return m.ready; });
    REQUIRE(map.ready);
    CHECK(map.generation == 1);
    check_two_blocks(map.levels, 10);
    // Nothing more arrives.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(mapper.snapshot().generation == 1);
}

TEST_CASE("Clearing or destroying the mapper stops a long walk within 100 ms", "[cassette][signal_map]") {
    const auto slow = long_silent_wav();
    std::atomic<bool> opened{false};
    auto open = [slow, &opened] {
        opened = true;
        return open_tape_image(*slow, "slow.wav");
    };
    {
        // The walk is long: unstopped, it has not finished after 200 ms.
        beebium::SignalMapper mapper;
        mapper.map(open);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK_FALSE(mapper.ready());
    }
    SECTION("cleared, then mapped again, the new job runs at once") {
        beebium::SignalMapper mapper;
        mapper.map(open);
        while (!opened) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto start = std::chrono::steady_clock::now();
        mapper.clear();
        const auto quick = std::make_shared<const std::vector<uint8_t>>(two_blocks().uef());
        mapper.map([quick] { return open_tape_image(*quick, "quick.uef"); });
        const auto map = wait_for(mapper, [](const auto& m) { return m.ready; });
        const auto elapsed = std::chrono::steady_clock::now() - start;
        REQUIRE(map.ready);
        CHECK(map.levels.size() == 12);
        CHECK(elapsed < std::chrono::milliseconds(100));
    }
    SECTION("destroyed") {
        auto mapper = std::make_unique<beebium::SignalMapper>();
        mapper->map(open);
        while (!opened) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto start = std::chrono::steady_clock::now();
        mapper.reset();
        CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
    }
}

namespace {

template <typename Done>
beebium::SignalMapper::Map wait_for_deck(const beebium::CassetteDeck& deck, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto map = deck.signal_map();
    while (!done(map) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        map = deck.signal_map();
    }
    return map;
}

}  // namespace

TEST_CASE("The deck maps a tape on insert and forgets it on eject", "[cassette][signal_map]") {
    beebium::CassetteDeck deck;
    const auto builder = two_blocks();
    beebium::test::ScopedTempDir dir("beebium_signal_map_");
    auto ready = [](const auto& m) { return m.ready; };

    SECTION("a CSW from its file") {
        const auto filepath = dir.path() / "two_blocks.csw";
        TapeBuilder::write(filepath, builder.csw());
        deck.insert(beebium::open_tape_file(filepath), "two_blocks.csw", "", filepath);
        const auto map = wait_for_deck(deck, ready);
        REQUIRE(map.ready);
        check_two_blocks(map.levels, 10);
        CHECK(deck.published().signal_map_ready);
        CHECK(deck.published().signal_map_generation == map.generation);
        deck.eject();
        CHECK_FALSE(deck.signal_map().ready);
        CHECK_FALSE(deck.published().signal_map_ready);
    }
    SECTION("a stereo WAV from its file, on the channel played") {
        WavOptions wav;
        wav.channels = 2;
        wav.signal_channel = 1;
        const auto filepath = dir.path() / "stereo.wav";
        TapeBuilder::write(filepath, builder.wav(wav));
        deck.insert(beebium::open_tape_file(filepath), "stereo.wav", "", filepath);
        const auto map = wait_for_deck(deck, ready);
        REQUIRE(map.ready);
        check_matches(map.levels, map_of(builder.uef(), "two_blocks.uef"), 16);
    }
    SECTION("a UEF with no file, from its image") {
        deck.insert(open_tape_image(builder.uef(), "two_blocks.uef"), "two_blocks.uef");
        const auto map = wait_for_deck(deck, ready);
        REQUIRE(map.ready);
        check_two_blocks(map.levels, 10);
    }
    SECTION("a CSW with no file has none") {
        deck.insert(open_tape_image(builder.csw(), "two_blocks.csw"), "two_blocks.csw");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(deck.signal_map().ready);
    }
    SECTION("a live tape has none") {
        auto source = std::make_shared<beebium::LiveSource>(48000);
        deck.insert(std::make_unique<beebium::LiveTape>(source, ""), "Line in");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(deck.signal_map().ready);
        CHECK(deck.signal_map().generation == 0);
    }
}

// =============================================================================
// The map in the recording head's wake
// =============================================================================

namespace {

struct WakePublish {
    uint64_t head_second;            // the second under the head at the publish
    beebium::TapeTime head;          // where the head was
    std::vector<uint8_t> levels;
};

// Record from `start` seconds: tone (an edge every 256 clocks, about 2400 Hz)
// where `tone(t)` says, silence elsewhere, for `seconds`, capturing the map at
// every publish; then stop.
template <typename Tone>
std::vector<WakePublish> record_wake(beebium::CassetteDeck& deck, double start, double seconds, Tone tone,
                                     uint32_t* final_generation = nullptr) {
    deck.seek(beebium::seconds_to_tape_time(start));
    deck.record();
    std::vector<WakePublish> publishes;
    uint32_t generation = deck.published().signal_map_generation;
    const auto clocks = beebium::seconds_to_tape_time(seconds);
    bool level = false;
    for (beebium::TapeTime i = 0; i < clocks; ++i) {
        const beebium::TapeTime head = deck.position();
        deck.clock(true);
        if (head % 256 == 0 && tone(static_cast<double>(head) / beebium::ULA_CLOCK_HZ)) {
            level = !level;
            deck.edge(head, level);
        }
        const uint32_t now = deck.published().signal_map_generation;
        if (now != generation) {
            generation = now;
            publishes.push_back({beebium::signal_map_second_of(head), head, deck.signal_map().levels});
        }
    }
    deck.stop();
    if (final_generation) *final_generation = generation;
    return publishes;
}

}  // namespace

TEST_CASE("While recording, the map follows the head a second at a time", "[cassette][signal_map][recording]") {
    beebium::CassetteDeck deck;
    TapeBuilder blank;
    blank.gap(60.0);
    deck.insert(open_tape_image(blank.uef(), "blank.uef"), "blank.uef");
    const auto before = wait_for_deck(deck, [](const auto& m) { return m.ready; });
    REQUIRE(before.ready);
    REQUIRE(before.levels.size() == 60);

    // From 0.5 s: tone for 3 s, silence for 2 s, tone for 1 s.
    uint32_t generation = 0;
    const auto publishes = record_wake(deck, 0.5, 6.0, [](double t) { return t < 3.5 || t >= 5.5; }, &generation);
    CHECK(publishes.size() >= 5);
    CHECK(publishes.size() <= 7);
    CHECK(generation == before.generation + publishes.size());

    // What each second reads once the head has passed it: second 0 is the half
    // the recording covered, all tone.
    const int complete[6] = {255, 255, 255, 128, 0, 128};
    for (const auto& publish : publishes) {
        CAPTURE(publish.head_second, publish.head);
        REQUIRE(publish.levels.size() == 60);
        for (uint64_t s = 0; s < publish.head_second; ++s) {
            CAPTURE(s, static_cast<int>(publish.levels[s]));
            CHECK(std::abs(static_cast<int>(publish.levels[s]) - complete[s]) <= 3);
        }
        // The second under the head: the passed half of it, as the schedule has it.
        // (Publishes fall half a second into a second, so its passed part is
        // one half of the schedule.)
        const double into = static_cast<double>(publish.head) / beebium::ULA_CLOCK_HZ;
        const double middle = (static_cast<double>(publish.head_second) + into) / 2.0;
        const bool tone_half = middle < 3.5 || middle >= 5.5;
        CAPTURE(static_cast<int>(publish.levels[publish.head_second]));
        CHECK(std::abs(static_cast<int>(publish.levels[publish.head_second]) - (tone_half ? 255 : 0)) <= 3);
        // Seconds ahead keep the blank's silence.
        for (uint64_t s = publish.head_second + 1; s < 60; ++s) CHECK(publish.levels[s] == 0);
    }

    // The recording's end: the walk of the image replaces the wake, one
    // generation on, and agrees with it.
    const auto after = wait_for_deck(deck, [&](const auto& m) { return m.generation > generation; });
    REQUIRE(after.ready);
    CHECK(after.generation == generation + 1);
    const int walked[7] = {128, 255, 255, 128, 0, 128, 128};  // second 0 and 6 are half recorded
    for (size_t s = 0; s < 7; ++s) {
        CAPTURE(s, static_cast<int>(after.levels[s]));
        CHECK(std::abs(static_cast<int>(after.levels[s]) - walked[s]) <= 3);
    }
}

TEST_CASE("Recording silence over signal erases it in the head's wake", "[cassette][signal_map][recording]") {
    beebium::CassetteDeck deck;
    TapeBuilder carrier;
    carrier.carrier(24000);  // 10 s
    deck.insert(open_tape_image(carrier.uef(), "carrier.uef"), "carrier.uef");
    const auto before = wait_for_deck(deck, [](const auto& m) { return m.ready; });
    REQUIRE(before.ready);
    REQUIRE(before.levels.size() == 10);

    const auto publishes = record_wake(deck, 0.0, 4.5, [](double) { return false; });
    REQUIRE_FALSE(publishes.empty());
    for (const auto& publish : publishes) {
        CAPTURE(publish.head_second);
        for (uint64_t s = 0; s < publish.head_second; ++s) CHECK(publish.levels[s] == 0);
        for (uint64_t s = publish.head_second + 1; s < 10; ++s) CHECK(publish.levels[s] == 255);
    }
    CHECK(publishes.back().head_second >= 3);
}

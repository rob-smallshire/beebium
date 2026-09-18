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

// Drop-detection contract for AudioBuffer. When the buffer overflows it drops
// samples silently, but each delivered chunk carries the produced index of its
// first sample (last_read_index), so a consumer sees dropped samples as a jump
// in that index beyond the previous chunk's length.

#include <catch2/catch_test_macros.hpp>

#include "beebium/AudioBuffer.hpp"

#include <vector>

using namespace beebium;

namespace {
AudioSample make_sample(uint32_t value) {
    AudioSample s;
    s.sources[0] = value;
    return s;
}
}  // namespace

TEST_CASE("AudioBuffer delivers contiguous produced indices with no drops",
          "[audio_buffer]") {
    AudioBuffer buffer(64);
    for (uint32_t i = 0; i < 40; ++i) {
        REQUIRE(buffer.push(make_sample(i)));
    }
    REQUIRE(buffer.dropped() == 0);
    REQUIRE(buffer.sequence() == 40);

    std::vector<AudioSample> dest(16);
    // First chunk: produced indices 0..15.
    REQUIRE(buffer.read(dest.data(), 16) == 16);
    REQUIRE(buffer.last_read_index() == 0);
    // Second chunk: produced indices 16..31, contiguous with the first.
    REQUIRE(buffer.read(dest.data(), 16) == 16);
    REQUIRE(buffer.last_read_index() == 16);
}

TEST_CASE("AudioBuffer overflow leaves a detectable gap in the produced index",
          "[audio_buffer]") {
    AudioBuffer buffer(8);

    // Fill the buffer until push() reports it is full. The push that reports
    // "full" is itself a dropped sample, so at least one drop is recorded here.
    uint64_t kept = 0;
    while (buffer.push(make_sample(static_cast<uint32_t>(kept)))) {
        ++kept;
    }
    REQUIRE(kept > 0);

    // A few more pushes are dropped (buffer still full), each counted as produced.
    for (uint64_t i = 0; i < 5; ++i) {
        REQUIRE_FALSE(buffer.push(make_sample(0xDEAD)));
    }
    const uint64_t drops = buffer.dropped();
    REQUIRE(drops >= 6);  // the terminating push plus the five above

    // Simulate a consumer tracking the produced index across chunks. The total
    // gap it observes equals the exact drop count (the drops are surfaced on the
    // first read after them rather than at their precise stream position).
    uint64_t expected_next = 0;
    uint64_t observed_gap = 0;

    // First chunk: drain everything currently buffered.
    std::vector<AudioSample> dest(static_cast<size_t>(kept) + 1);
    size_t n1 = buffer.read(dest.data(), static_cast<size_t>(kept));
    REQUIRE(n1 == kept);
    observed_gap += buffer.last_read_index() - expected_next;
    expected_next = buffer.last_read_index() + n1;

    // Second chunk: one more sample, pushed after the drops.
    REQUIRE(buffer.push(make_sample(0xBEEF)));
    size_t n2 = buffer.read(dest.data(), 1);
    REQUIRE(n2 == 1);
    observed_gap += buffer.last_read_index() - expected_next;

    REQUIRE(observed_gap == drops);
}

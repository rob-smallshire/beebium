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

// AudioFanOut shares one AudioBuffer among several audio streams (#236):
// each subscriber sees every sample once, in its place in the produced
// stream, and a full subscriber drops only its own samples.

#include <catch2/catch_test_macros.hpp>

#include "beebium/service/AudioFanOut.hpp"

#include <cstdint>
#include <vector>

using beebium::AudioBuffer;
using beebium::AudioSample;
using beebium::service::AudioFanOut;

namespace {

// Push `count` samples whose field 0 is their produced index.
void produce(AudioBuffer& buffer, uint32_t& next, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        AudioSample sample;
        sample.sources[0] = next++;
        REQUIRE(buffer.push(sample));
    }
}

struct Chunk {
    uint64_t first_index = 0;
    std::vector<uint32_t> values;
};

Chunk take(AudioFanOut& fan_out, AudioBuffer& buffer, AudioFanOut::Subscriber& subscriber,
           size_t max_count, size_t min_count = 1) {
    std::vector<AudioSample> samples(max_count);
    Chunk chunk;
    const size_t n = fan_out.drain_and_take(buffer, subscriber, samples.data(), min_count,
                                            max_count, chunk.first_index);
    for (size_t i = 0; i < n; ++i) chunk.values.push_back(samples[i].sources[0]);
    return chunk;
}

// Every value equals its produced index.
bool in_place(const Chunk& chunk) {
    for (size_t i = 0; i < chunk.values.size(); ++i) {
        if (chunk.values[i] != chunk.first_index + i) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("AudioFanOut gives each subscriber every sample", "[audio][fan-out]") {
    AudioBuffer buffer(1000);
    AudioFanOut fan_out(1000);
    auto a = fan_out.subscribe();
    auto b = fan_out.subscribe();
    uint32_t next = 0;

    produce(buffer, next, 300);
    const Chunk a1 = take(fan_out, buffer, *a, 200);
    const Chunk b1 = take(fan_out, buffer, *b, 300);
    produce(buffer, next, 100);
    const Chunk a2 = take(fan_out, buffer, *a, 300);
    const Chunk b2 = take(fan_out, buffer, *b, 300);

    CHECK(a1.first_index == 0);
    CHECK(a1.values.size() == 200);
    CHECK(a2.first_index == 200);
    CHECK(a2.values.size() == 200);
    CHECK(b1.first_index == 0);
    CHECK(b1.values.size() == 300);
    CHECK(b2.first_index == 300);
    CHECK(b2.values.size() == 100);
    CHECK((in_place(a1) && in_place(a2) && in_place(b1) && in_place(b2)));
    CHECK(a->dropped() == 0);
    CHECK(b->dropped() == 0);
}

TEST_CASE("AudioFanOut takes nothing until a subscriber holds the minimum", "[audio][fan-out]") {
    AudioBuffer buffer(1000);
    AudioFanOut fan_out(1000);
    auto a = fan_out.subscribe();
    uint32_t next = 0;

    produce(buffer, next, 100);
    CHECK(take(fan_out, buffer, *a, 256, 256).values.empty());
    produce(buffer, next, 200);
    const Chunk chunk = take(fan_out, buffer, *a, 256, 256);
    CHECK(chunk.first_index == 0);
    CHECK(chunk.values.size() == 256);
}

TEST_CASE("AudioFanOut drops only a full subscriber's own samples, and its gap says how many",
          "[audio][fan-out]") {
    AudioBuffer buffer(1000);
    AudioFanOut fan_out(100);
    auto fast = fan_out.subscribe();
    auto slow = fan_out.subscribe();
    uint32_t next = 0;

    // The fast subscriber drains and takes as samples arrive; the slow one
    // takes nothing until its queue of 100 has overflowed by 60.
    produce(buffer, next, 80);
    const Chunk fast1 = take(fan_out, buffer, *fast, 100);
    produce(buffer, next, 80);
    const Chunk fast2 = take(fan_out, buffer, *fast, 100);
    CHECK(slow->dropped() == 60);
    CHECK(fast->dropped() == 0);

    const Chunk slow1 = take(fan_out, buffer, *slow, 1000);
    produce(buffer, next, 40);
    const Chunk slow2 = take(fan_out, buffer, *slow, 1000);
    const Chunk fast3 = take(fan_out, buffer, *fast, 100);

    CHECK(fast1.first_index == 0);
    CHECK(fast2.first_index == 80);
    CHECK(fast3.first_index == 160);
    CHECK(fast3.values.size() == 40);
    CHECK((in_place(fast1) && in_place(fast2) && in_place(fast3)));

    CHECK(slow1.first_index == 0);
    CHECK(slow1.values.size() == 100);
    CHECK(slow2.first_index == 160);
    CHECK(slow2.first_index - (slow1.first_index + slow1.values.size()) == slow->dropped());
    CHECK((in_place(slow1) && in_place(slow2)));
}

TEST_CASE("AudioFanOut ends a chunk at a gap so its index places every sample",
          "[audio][fan-out]") {
    AudioBuffer buffer(1000);
    AudioFanOut fan_out(100);
    auto fast = fan_out.subscribe();
    auto slow = fan_out.subscribe();
    uint32_t next = 0;

    // The slow queue keeps 0..99 and drops 100..149; once it has room, it
    // keeps 150..179. A take of everything it holds stops at the gap.
    produce(buffer, next, 150);
    take(fan_out, buffer, *fast, 1000);
    const Chunk slow1 = take(fan_out, buffer, *slow, 50);  // 0..49
    produce(buffer, next, 30);
    const Chunk slow2 = take(fan_out, buffer, *slow, 1000);
    const Chunk slow3 = take(fan_out, buffer, *slow, 1000);

    CHECK(slow1.first_index == 0);
    CHECK(slow2.first_index == 50);
    CHECK(slow2.values.size() == 50);  // 50..99, up to the gap
    CHECK(slow->dropped() == 50);
    CHECK(slow3.first_index == 150);
    CHECK(slow3.values.size() == 30);
    CHECK((in_place(slow1) && in_place(slow2) && in_place(slow3)));
}

TEST_CASE("AudioFanOut starts a late subscriber at the current position", "[audio][fan-out]") {
    AudioBuffer buffer(1000);
    AudioFanOut fan_out(1000);
    auto early = fan_out.subscribe();
    uint32_t next = 0;

    produce(buffer, next, 30);
    take(fan_out, buffer, *early, 1000);
    auto late = fan_out.subscribe();
    produce(buffer, next, 10);
    const Chunk chunk = take(fan_out, buffer, *late, 1000);

    CHECK(chunk.first_index == 30);
    CHECK(chunk.values.size() == 10);
    CHECK(in_place(chunk));
}

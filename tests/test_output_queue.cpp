// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// OutputQueue as a single-producer, single-consumer ring: one thread
// producing and another consuming concurrently, with no external lock.

#include <beebium/OutputQueue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using beebium::OutputQueue;

namespace {

// A small capacity that is not a power of two, so the indices wrap at every
// position many times over.
constexpr size_t kCapacity = 7;
constexpr uint64_t kItemCount = 500'000;

// Consumes kItemCount items, checking that they arrive in the order they
// were produced and that the queue never reports more than its capacity.
// Returns the number of order or capacity violations seen.
uint64_t consume_all(OutputQueue<uint64_t>& queue) {
    uint64_t expected = 0;
    uint64_t violations = 0;
    while (expected < kItemCount) {
        auto buffers = queue.get_consumer_buffer();
        if (buffers.total() > queue.capacity()) ++violations;
        for (uint64_t value : buffers.a) {
            if (value != expected) ++violations;
            ++expected;
        }
        for (uint64_t value : buffers.b) {
            if (value != expected) ++violations;
            ++expected;
        }
        queue.consume(buffers.total());
        if (buffers.empty()) std::this_thread::yield();
    }
    return violations;
}

}  // namespace

TEST_CASE("OutputQueue keeps order under a concurrent producer and consumer, item by item",
          "[output_queue]") {
    OutputQueue<uint64_t> queue(kCapacity);
    std::atomic<uint64_t> violations{0};
    std::thread consumer([&] { violations = consume_all(queue); });
    for (uint64_t value = 0; value < kItemCount;) {
        if (queue.push(value)) {
            ++value;
        } else {
            std::this_thread::yield();
        }
    }
    consumer.join();
    CHECK(violations == 0);
    CHECK(queue.empty());
}

TEST_CASE("OutputQueue keeps order under a concurrent producer and consumer, in batches",
          "[output_queue]") {
    OutputQueue<uint64_t> queue(kCapacity);
    std::atomic<uint64_t> violations{0};
    std::thread consumer([&] { violations = consume_all(queue); });
    uint64_t overruns = 0;
    for (uint64_t value = 0; value < kItemCount;) {
        auto buffers = queue.get_producer_buffer();
        if (buffers.total() > kCapacity) ++overruns;
        size_t written = 0;
        for (auto& slot : buffers.a) {
            if (value + written == kItemCount) break;
            slot = value + written;
            ++written;
        }
        for (auto& slot : buffers.b) {
            if (value + written == kItemCount) break;
            slot = value + written;
            ++written;
        }
        queue.produce(written);
        value += written;
        if (written == 0) std::this_thread::yield();
    }
    consumer.join();
    CHECK(overruns == 0);
    CHECK(violations == 0);
    CHECK(queue.empty());
}

TEST_CASE("OutputQueue reports full and empty across a wrap", "[output_queue]") {
    OutputQueue<int> queue(3);
    REQUIRE(queue.empty());
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    queue.consume(1);  // The next pushes wrap past the end.
    REQUIRE(queue.push(3));
    REQUIRE(queue.push(4));
    CHECK(queue.full());
    CHECK(queue.available() == 0);
    CHECK_FALSE(queue.push(5));
    CHECK(queue.get_producer_buffer().empty());

    auto buffers = queue.get_consumer_buffer();
    REQUIRE(buffers.total() == 3);
    CHECK(buffers.a.size() == 2);
    CHECK(buffers.a[0] == 2);
    CHECK(buffers.a[1] == 3);
    REQUIRE(buffers.b.size() == 1);
    CHECK(buffers.b[0] == 4);
    queue.consume(3);
    CHECK(queue.empty());
    CHECK(queue.get_consumer_buffer().empty());
    CHECK(queue.available() == 3);
}

// Each side keeps a cached copy of the other side's index and reloads it only
// when the cache says the ring is full (producer) or empty (consumer). A stale
// cache must only ever understate: the producer never overwrites a slot the
// consumer has not released, and the consumer never reads one the producer
// has not published.

TEST_CASE("OutputQueue producer with a stale cache of a full ring reloads it",
          "[output_queue]") {
    OutputQueue<int> queue(4);
    for (int value = 0; value < 4; ++value) REQUIRE(queue.push(value));
    REQUIRE_FALSE(queue.push(99));  // The producer's cache now records a full ring.

    // The consumer frees two slots; the producer's cache still says full.
    auto buffers = queue.get_consumer_buffer();
    REQUIRE(buffers.total() == 4);
    queue.consume(2);

    // Exactly the two freed slots can be filled (across the wrap), no more.
    CHECK(queue.push(4));
    CHECK(queue.push(5));
    CHECK_FALSE(queue.push(6));
    CHECK(queue.get_producer_buffer().empty());

    // Nothing unread was overwritten: the ring holds 2, 3, 4, 5 in order.
    std::vector<int> seen;
    while (true) {
        auto readable = queue.get_consumer_buffer();
        if (readable.empty()) break;
        seen.insert(seen.end(), readable.a.begin(), readable.a.end());
        seen.insert(seen.end(), readable.b.begin(), readable.b.end());
        queue.consume(readable.total());
    }
    CHECK(seen == std::vector<int>{2, 3, 4, 5});
}

TEST_CASE("OutputQueue producer's stale cache never exceeds the real free space",
          "[output_queue]") {
    OutputQueue<int> queue(5);
    REQUIRE(queue.push(0));
    REQUIRE(queue.push(1));
    // Free space as the producer sees it is at most the real free space.
    CHECK(queue.get_producer_buffer().total() <= queue.available());
    queue.get_consumer_buffer();
    queue.consume(2);
    CHECK(queue.get_producer_buffer().total() <= queue.available());
    // Filling what the producer offers never overruns the ring.
    size_t pushed = 0;
    while (queue.push(static_cast<int>(10 + pushed))) ++pushed;
    CHECK(queue.size() == queue.capacity());
    CHECK(pushed == queue.capacity());
}

TEST_CASE("OutputQueue consumer with a stale cache of an empty ring reloads it",
          "[output_queue]") {
    OutputQueue<int> queue(4);
    CHECK(queue.get_consumer_buffer().empty());  // The cache records an empty ring.
    REQUIRE(queue.push(7));
    REQUIRE(queue.push(8));
    auto buffers = queue.get_consumer_buffer();
    REQUIRE(buffers.total() == 2);
    CHECK(buffers.a[0] == 7);
    CHECK(buffers.a[1] == 8);
}

TEST_CASE("OutputQueue consumer's stale cache shows only published items, in order",
          "[output_queue]") {
    OutputQueue<int> queue(4);
    REQUIRE(queue.push(0));
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE(queue.get_consumer_buffer().total() == 3);
    queue.consume(2);  // One item left; the consumer's cache says so.
    REQUIRE(queue.push(3));
    REQUIRE(queue.push(4));  // Wraps.

    // Whatever the consumer is shown is a correct, in-order prefix of the
    // unread items, and draining what it is shown reaches all of them.
    std::vector<int> seen;
    for (int pass = 0; pass < 4; ++pass) {
        auto readable = queue.get_consumer_buffer();
        CHECK(readable.total() <= queue.size());
        seen.insert(seen.end(), readable.a.begin(), readable.a.end());
        seen.insert(seen.end(), readable.b.begin(), readable.b.end());
        queue.consume(readable.total());
    }
    CHECK(seen == std::vector<int>{2, 3, 4});
    CHECK(queue.empty());
}

TEST_CASE("OutputQueue reset clears both sides' caches", "[output_queue]") {
    OutputQueue<int> queue(2);
    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE_FALSE(queue.push(3));
    REQUIRE(queue.get_consumer_buffer().total() == 2);
    queue.reset();
    CHECK(queue.empty());
    CHECK(queue.get_consumer_buffer().empty());
    CHECK(queue.push(4));
    CHECK(queue.push(5));
    auto buffers = queue.get_consumer_buffer();
    REQUIRE(buffers.total() == 2);
    CHECK(buffers.a[0] == 4);
}

// Staged items: the producer can commit items to its own view with stage()
// and make them visible to the consumer later, all at once, with publish():
// one release store for many items.

TEST_CASE("OutputQueue staged items are invisible to the consumer until published",
          "[output_queue]") {
    OutputQueue<int> queue(8);
    for (int value = 0; value < 3; ++value) {
        auto slots = queue.get_producer_buffer();
        REQUIRE_FALSE(slots.empty());
        slots.a[0] = value;
        queue.stage(1);
    }
    CHECK(queue.unpublished() == 3);
    CHECK(queue.empty());
    CHECK(queue.get_consumer_buffer().empty());

    queue.publish();
    CHECK(queue.unpublished() == 0);
    CHECK(queue.size() == 3);
    auto buffers = queue.get_consumer_buffer();
    REQUIRE(buffers.total() == 3);
    CHECK(buffers.a[0] == 0);
    CHECK(buffers.a[1] == 1);
    CHECK(buffers.a[2] == 2);
}

TEST_CASE("OutputQueue publish with nothing staged changes nothing", "[output_queue]") {
    OutputQueue<int> queue(4);
    queue.publish();
    CHECK(queue.empty());
    REQUIRE(queue.push(1));  // push publishes at once
    CHECK(queue.unpublished() == 0);
    queue.publish();
    CHECK(queue.size() == 1);
}

TEST_CASE("OutputQueue producer's view counts staged items as used", "[output_queue]") {
    OutputQueue<int> queue(2);
    auto slots = queue.get_producer_buffer();
    REQUIRE(slots.total() == 2);
    slots.a[0] = 1;
    slots.a[1] = 2;
    queue.stage(2);
    // Staged but unpublished, the slots are still the producer's: none is free.
    CHECK(queue.get_producer_buffer().empty());
    CHECK_FALSE(queue.push(3));
    queue.publish();
    CHECK(queue.full());
}

TEST_CASE("OutputQueue keeps order with a concurrent consumer when items are published in bursts",
          "[output_queue]") {
    OutputQueue<uint64_t> queue(kCapacity);
    std::atomic<uint64_t> violations{0};
    std::thread consumer([&] { violations = consume_all(queue); });
    for (uint64_t value = 0; value < kItemCount;) {
        auto slots = queue.get_producer_buffer();
        if (slots.empty()) {
            queue.publish();  // a full ring of staged items: let the consumer have them
            std::this_thread::yield();
            continue;
        }
        slots.a[0] = value++;
        queue.stage(1);
        if (value % 3 == 0) queue.publish();
    }
    queue.publish();
    consumer.join();
    CHECK(violations == 0);
    CHECK(queue.empty());
}

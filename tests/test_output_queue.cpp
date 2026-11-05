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

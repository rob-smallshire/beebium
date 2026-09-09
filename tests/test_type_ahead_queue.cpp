// Copyright (c) 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// test_type_ahead_queue.cpp
//
// Unit tests for TypeAheadQueue in isolation: the key press/release state
// machine against a bare KeyboardMatrix, the cross-thread status queries, and
// the emulation-thread cost of an idle queue. tick() runs on every host cycle
// (2 MHz), so the idle path must not take the queue mutex.

#include <catch2/catch_test_macros.hpp>

#include <beebium/KeyboardMapping.hpp>
#include <beebium/KeyboardMatrix.hpp>
#include <beebium/TypeAheadQueue.hpp>

#include <chrono>
#include <future>
#include <mutex>
#include <thread>

using namespace beebium;

namespace {

constexpr size_t HOLD = 10;
constexpr size_t GAP = 5;

void tick_n(TypeAheadQueue& queue, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        queue.tick();
    }
}

KeyMapping mapping_for(char32_t codepoint) {
    auto mapping = char_to_key(codepoint);
    REQUIRE(mapping.has_value());
    return *mapping;
}

bool pressed(const KeyboardMatrix& keyboard, const KeyMapping& mapping) {
    return keyboard.is_key_pressed(mapping.row(), mapping.column());
}

bool shift_pressed(const KeyboardMatrix& keyboard) {
    return keyboard.is_key_pressed(0, 0);
}

} // namespace

TEST_CASE("TypeAheadQueue: idle tick does not touch the keyboard or the status", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);

    const auto sequence_before = queue.status_sequence();
    tick_n(queue, 1000);

    CHECK(queue.empty());
    CHECK(queue.pending_characters() == 0);
    CHECK(queue.strings_queued() == 0);
    CHECK(queue.status_sequence() == sequence_before);
    for (uint8_t column = 0; column < KeyboardMatrix::NUM_COLUMNS; ++column) {
        CHECK(keyboard.read_column(column) == 0);
    }
}

TEST_CASE("TypeAheadQueue: idle tick takes no lock", "[keyboard][typeahead]") {
    // Hold the queue's mutex from this thread and tick the idle queue on a
    // worker. If tick() locked on the idle path the worker would block and
    // the wait below would expire.
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);

    std::unique_lock<std::mutex> held(queue.queue_mutex_for_test());

    auto ticks = std::async(std::launch::async, [&queue] {
        tick_n(queue, 100000);
        return true;
    });

    const auto status = ticks.wait_for(std::chrono::seconds(10));
    held.unlock();
    REQUIRE(status == std::future_status::ready);
    CHECK(ticks.get());
}

TEST_CASE("TypeAheadQueue: types a lower-case letter with hold and gap", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);
    const auto a = mapping_for(U'a');

    REQUIRE(queue.enqueue("a", HOLD, GAP));
    CHECK_FALSE(queue.empty());
    CHECK(queue.pending_characters() == 1);
    CHECK(queue.strings_queued() == 1);

    // The first tick pops the string and presses the key.
    queue.tick();
    CHECK(pressed(keyboard, a));
    CHECK_FALSE(shift_pressed(keyboard));
    CHECK(queue.strings_queued() == 1);
    CHECK(queue.pending_characters() == 1);

    // Held for HOLD ticks, then released.
    tick_n(queue, HOLD - 1);
    CHECK(pressed(keyboard, a));
    queue.tick();
    CHECK_FALSE(pressed(keyboard, a));

    // Released for GAP ticks, then the string is complete.
    tick_n(queue, GAP - 1);
    CHECK_FALSE(queue.empty());
    queue.tick();
    CHECK(queue.empty());
    CHECK(queue.pending_characters() == 0);
    CHECK(queue.strings_queued() == 0);
}

TEST_CASE("TypeAheadQueue: presses SHIFT with an upper-case letter and releases both", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);
    const auto a = mapping_for(U'A');
    REQUIRE(a.needs_shift);

    REQUIRE(queue.enqueue("A", HOLD, GAP));
    queue.tick();
    CHECK(pressed(keyboard, a));
    CHECK(shift_pressed(keyboard));

    tick_n(queue, HOLD);
    CHECK_FALSE(pressed(keyboard, a));
    CHECK_FALSE(shift_pressed(keyboard));
}

TEST_CASE("TypeAheadQueue: consumes queued strings in order", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);
    const auto a = mapping_for(U'a');
    const auto b = mapping_for(U'b');

    REQUIRE(queue.enqueue("a", HOLD, GAP));
    REQUIRE(queue.enqueue("b", HOLD, GAP));
    CHECK(queue.pending_characters() == 2);
    CHECK(queue.strings_queued() == 2);

    queue.tick();
    CHECK(pressed(keyboard, a));
    CHECK(queue.strings_queued() == 2);

    // Finish "a": the hold, then the gap. The next tick starts "b".
    tick_n(queue, HOLD + GAP);
    CHECK(queue.pending_characters() == 1);
    CHECK(queue.strings_queued() == 1);
    queue.tick();
    CHECK(pressed(keyboard, b));
    CHECK_FALSE(pressed(keyboard, a));
}

TEST_CASE("TypeAheadQueue: rejects untypeable text without queuing it", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);

    const auto sequence_before = queue.status_sequence();
    CHECK_FALSE(queue.enqueue("\x01"));
    CHECK(queue.empty());
    CHECK(queue.status_sequence() == sequence_before);
}

TEST_CASE("TypeAheadQueue: clear reports the pending count and empties the queue", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);
    const auto a = mapping_for(U'a');

    REQUIRE(queue.enqueue("abc", HOLD, GAP));
    REQUIRE(queue.enqueue("de", HOLD, GAP));
    queue.tick();
    REQUIRE(pressed(keyboard, a));

    // The held key belongs to the emulation thread. clear() reports the
    // count and marks the queue empty at once; the key itself is released on
    // the next tick, which is the next host cycle.
    CHECK(queue.clear() == 5);
    CHECK(queue.empty());
    CHECK(queue.pending_characters() == 0);
    CHECK(queue.strings_queued() == 0);
    CHECK(pressed(keyboard, a));

    queue.tick();
    CHECK_FALSE(pressed(keyboard, a));
    CHECK_FALSE(shift_pressed(keyboard));
    CHECK(queue.empty());

    // Nothing lingers: further ticks press nothing.
    tick_n(queue, HOLD + GAP);
    for (uint8_t column = 0; column < KeyboardMatrix::NUM_COLUMNS; ++column) {
        CHECK(keyboard.read_column(column) == 0);
    }
}

TEST_CASE("TypeAheadQueue: clear while idle is a no-op", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);

    CHECK(queue.clear() == 0);
    CHECK(queue.empty());
    queue.tick();
    CHECK(queue.empty());
}

TEST_CASE("TypeAheadQueue: text enqueued after clear is typed", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);
    const auto a = mapping_for(U'a');
    const auto b = mapping_for(U'b');

    REQUIRE(queue.enqueue("a", HOLD, GAP));
    queue.tick();
    REQUIRE(pressed(keyboard, a));
    queue.clear();
    REQUIRE(queue.enqueue("b", HOLD, GAP));

    // One tick cancels the held key; the next starts the new string.
    queue.tick();
    CHECK_FALSE(pressed(keyboard, a));
    queue.tick();
    CHECK(pressed(keyboard, b));
}

TEST_CASE("TypeAheadQueue: status sequence advances on each observable change", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);

    auto last = queue.status_sequence();
    auto expect_advanced = [&] {
        const auto now = queue.status_sequence();
        CHECK(now > last);
        last = now;
    };
    auto expect_unchanged = [&] {
        CHECK(queue.status_sequence() == last);
    };

    REQUIRE(queue.enqueue("ab", HOLD, GAP));
    expect_advanced();

    queue.tick();               // string started
    expect_advanced();
    tick_n(queue, HOLD);        // key released: not a status change
    expect_unchanged();
    tick_n(queue, GAP);         // first character consumed
    expect_advanced();
    tick_n(queue, HOLD + GAP);  // second character consumed, string finished
    expect_advanced();

    tick_n(queue, 100);         // idle
    expect_unchanged();

    REQUIRE(queue.enqueue("c", HOLD, GAP));
    expect_advanced();
    queue.clear();
    expect_advanced();
}

TEST_CASE("TypeAheadQueue: enqueue from another thread is picked up by tick", "[keyboard][typeahead]") {
    KeyboardMatrix keyboard;
    TypeAheadQueue queue(keyboard);
    const auto a = mapping_for(U'a');

    std::thread producer([&queue] {
        REQUIRE(queue.enqueue("a", HOLD, GAP));
    });
    producer.join();

    queue.tick();
    CHECK(pressed(keyboard, a));
}

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

#include "connection/FrameMailbox.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

using namespace beebium::linux_client;

namespace {

std::unique_ptr<DecodedFrame> frame_numbered(std::uint64_t number) {
    auto frame = std::make_unique<DecodedFrame>();
    frame->frame_number = number;
    return frame;
}

}  // namespace

TEST_CASE("An empty mailbox yields nothing", "[connection][mailbox]") {
    FrameMailbox mailbox;
    CHECK(mailbox.take() == nullptr);
}

TEST_CASE("A published frame is taken once", "[connection][mailbox]") {
    FrameMailbox mailbox;
    CHECK(mailbox.publish(frame_numbered(1)));
    auto frame = mailbox.take();
    REQUIRE(frame != nullptr);
    CHECK(frame->frame_number == 1);
    CHECK(mailbox.take() == nullptr);
}

TEST_CASE("A newer frame replaces an untaken one", "[connection][mailbox]") {
    FrameMailbox mailbox;
    CHECK(mailbox.publish(frame_numbered(1)));
    // The slot was full: no second notification is needed.
    CHECK_FALSE(mailbox.publish(frame_numbered(2)));
    CHECK_FALSE(mailbox.publish(frame_numbered(3)));

    auto frame = mailbox.take();
    REQUIRE(frame != nullptr);
    CHECK(frame->frame_number == 3);
    CHECK(mailbox.published_count() == 3);
    CHECK(mailbox.superseded_count() == 2);
}

TEST_CASE("Taking empties the slot so the next publish notifies again",
          "[connection][mailbox]") {
    FrameMailbox mailbox;
    CHECK(mailbox.publish(frame_numbered(1)));
    mailbox.take();
    CHECK(mailbox.publish(frame_numbered(2)));
}

TEST_CASE("The consumer only ever sees frames in increasing order",
          "[connection][mailbox][threads]") {
    FrameMailbox mailbox;
    constexpr std::uint64_t FRAME_COUNT = 20000;
    std::atomic<bool> done{false};

    std::thread producer([&] {
        for (std::uint64_t n = 1; n <= FRAME_COUNT; ++n) {
            mailbox.publish(frame_numbered(n));
        }
        done.store(true);
    });

    std::uint64_t last_seen = 0;
    bool in_order = true;
    for (;;) {
        const bool finished = done.load();
        if (auto frame = mailbox.take()) {
            in_order = in_order && frame->frame_number > last_seen;
            last_seen = frame->frame_number;
        } else if (finished) {
            break;
        }
    }
    producer.join();

    CHECK(in_order);
    CHECK(last_seen == FRAME_COUNT);
    CHECK(mailbox.published_count() == FRAME_COUNT);
}

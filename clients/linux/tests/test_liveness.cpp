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

// The liveness state machine of client specification 4.3.

#include "connection/Liveness.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace std::chrono_literals;

namespace {

const auto T0 = LivenessTracker::Clock::time_point{} + 100s;

}  // namespace

TEST_CASE("A connection starts active and stays so while events arrive", "[liveness]") {
    LivenessTracker tracker;
    CHECK(tracker.state() == Liveness::active);
    tracker.on_event(T0, false);
    CHECK_FALSE(tracker.on_tick(T0 + 1500ms));
    tracker.on_event(T0 + 1500ms, false);
    CHECK_FALSE(tracker.on_tick(T0 + 3000ms));
    CHECK(tracker.state() == Liveness::active);
}

TEST_CASE("Silence past the watchdog is unreachable, and an event recovers it",
          "[liveness]") {
    LivenessTracker tracker;
    tracker.on_event(T0, false);
    CHECK(tracker.on_tick(T0 + 2100ms));
    CHECK(tracker.state() == Liveness::unreachable);
    CHECK_FALSE(tracker.terminal());
    tracker.on_event(T0 + 5s, false);
    CHECK(tracker.state() == Liveness::active);
}

TEST_CASE("The watchdog is not armed before the first event", "[liveness]") {
    LivenessTracker tracker;
    CHECK_FALSE(tracker.on_tick(T0 + 1h));
    CHECK(tracker.state() == Liveness::active);
}

TEST_CASE("An announced shutdown then the stream's end is stopped", "[liveness]") {
    LivenessTracker tracker;
    tracker.on_event(T0, false);
    tracker.on_event(T0 + 1s, true);
    tracker.on_stream_end(false);
    CHECK(tracker.state() == Liveness::stopped);
    CHECK(tracker.terminal());
}

TEST_CASE("The stream's end with no announcement is died", "[liveness]") {
    LivenessTracker tracker;
    tracker.on_event(T0, false);
    tracker.on_stream_end(false);
    CHECK(tracker.state() == Liveness::died);
}

TEST_CASE("Unreachable then the stream's end escalates to died", "[liveness]") {
    LivenessTracker tracker;
    tracker.on_event(T0, false);
    tracker.on_tick(T0 + 3s);
    tracker.on_stream_end(false);
    CHECK(tracker.state() == Liveness::died);
}

TEST_CASE("Our own cancellation changes nothing", "[liveness]") {
    LivenessTracker tracker;
    tracker.on_event(T0, false);
    tracker.on_stream_end(true);
    CHECK(tracker.state() == Liveness::active);
}

TEST_CASE("Terminal states stay until a new connection", "[liveness]") {
    LivenessTracker tracker;
    tracker.on_event(T0, false);
    tracker.on_stream_end(false);
    tracker.on_event(T0 + 1s, false);
    CHECK_FALSE(tracker.on_tick(T0 + 1h));
    CHECK(tracker.state() == Liveness::died);
    tracker.reset();
    CHECK(tracker.state() == Liveness::active);
}

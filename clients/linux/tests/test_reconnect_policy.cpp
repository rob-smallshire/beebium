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

// The recovery policy and the window's connection verdict, client
// specification 4.3 and 4.4.

#include "connection/ReconnectPolicy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace beebium::linux_client;
using namespace std::chrono_literals;
using Kind = ReconnectPolicy::Step::Kind;
using Phase = ReconnectPolicy::Phase;

TEST_CASE("A lost connection is retried after 0.5 s, doubling to 8 s, eight times",
          "[reconnect]") {
    ReconnectPolicy policy;
    std::vector<std::chrono::milliseconds> delays;
    auto step = policy.on_connection_lost();
    while (step.kind == Kind::attempt_after) {
        delays.push_back(step.delay);
        REQUIRE(policy.on_timer().kind == Kind::attempt_now);
        CHECK(policy.phase() == Phase::attempting);
        step = policy.on_attempt_failed();
    }
    CHECK(delays == std::vector<std::chrono::milliseconds>{500ms, 1000ms, 2000ms, 4000ms,
                                                           8000ms, 8000ms, 8000ms, 8000ms});
    CHECK(policy.phase() == Phase::gave_up);
}

TEST_CASE("A successful attempt ends recovery", "[reconnect]") {
    ReconnectPolicy policy;
    policy.on_connection_lost();
    policy.on_timer();
    policy.on_connected();
    CHECK(policy.phase() == Phase::idle);
    CHECK(policy.attempt() == 0);
}

TEST_CASE("Retry after giving up tries at once from the first attempt", "[reconnect]") {
    ReconnectPolicy policy;
    auto step = policy.on_connection_lost();
    while (step.kind == Kind::attempt_after) {
        policy.on_timer();
        step = policy.on_attempt_failed();
    }
    REQUIRE(policy.phase() == Phase::gave_up);
    CHECK(policy.on_retry().kind == Kind::attempt_now);
    CHECK(policy.attempt() == 1);
    CHECK(policy.on_attempt_failed().delay == 1000ms);
}

TEST_CASE("A graceful stop is never fought, even mid-backoff", "[reconnect]") {
    ReconnectPolicy policy;
    policy.on_connection_lost();
    policy.on_graceful_stop();
    CHECK(policy.phase() == Phase::idle);
    CHECK(policy.on_timer().kind == Kind::none);
    // And waking does not revive it.
    CHECK(policy.on_wake(false).kind == Kind::none);
}

TEST_CASE("Closing the window cancels recovery for good", "[reconnect]") {
    ReconnectPolicy policy;
    policy.on_connection_lost();
    policy.on_window_closing();
    CHECK(policy.on_timer().kind == Kind::none);
    CHECK(policy.on_connection_lost().kind == Kind::none);
    CHECK(policy.on_wake(true).kind == Kind::none);
    CHECK(policy.on_retry().kind == Kind::none);
}

TEST_CASE("Waking tries at once and resets the backoff", "[reconnect]") {
    ReconnectPolicy policy;
    auto step = policy.on_connection_lost();
    for (int i = 0; i < 4; ++i) {
        policy.on_timer();
        step = policy.on_attempt_failed();
    }
    REQUIRE(step.delay == 8000ms);
    CHECK(policy.on_wake(false).kind == Kind::attempt_now);
    CHECK(policy.attempt() == 1);
    CHECK(policy.on_attempt_failed().delay == 1000ms);
}

TEST_CASE("Waking with a connection that looked live still rebuilds it", "[reconnect]") {
    // After full sleep a TCP connection is very likely dead, whatever the
    // last state said (frontend-sleep-wake.md).
    ReconnectPolicy policy;
    CHECK(policy.on_wake(true).kind == Kind::attempt_now);
}

TEST_CASE("Going to sleep cancels the pending attempt; waking resumes", "[reconnect]") {
    ReconnectPolicy policy;
    policy.on_connection_lost();
    policy.on_will_sleep();
    CHECK(policy.on_timer().kind == Kind::none);
    CHECK(policy.on_wake(false).kind == Kind::attempt_now);
}

TEST_CASE("The verdict is the liveness when nothing is recovering", "[verdict]") {
    CHECK(connection_verdict(Liveness::active, Phase::idle) == ConnectionVerdict::live);
    CHECK(connection_verdict(Liveness::unreachable, Phase::idle) ==
          ConnectionVerdict::unreachable);
    CHECK(connection_verdict(Liveness::stopped, Phase::idle) == ConnectionVerdict::stopped);
    CHECK(connection_verdict(Liveness::died, Phase::idle) == ConnectionVerdict::died);
}

TEST_CASE("Recovery outranks the raw liveness", "[verdict]") {
    CHECK(connection_verdict(Liveness::died, Phase::waiting) == ConnectionVerdict::reconnecting);
    CHECK(connection_verdict(Liveness::died, Phase::attempting) ==
          ConnectionVerdict::reconnecting);
    CHECK(connection_verdict(Liveness::died, Phase::gave_up) ==
          ConnectionVerdict::could_not_reconnect);
    CHECK(is_live(ConnectionVerdict::live));
    CHECK_FALSE(is_live(ConnectionVerdict::reconnecting));
}

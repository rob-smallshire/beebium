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

#include "metrics/IntervalStats.hpp"
#include "metrics/ThreadCpu.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using Catch::Approx;

TEST_CASE("An empty sample summarises to zeros", "[metrics]") {
    const auto summary = summarise({});
    CHECK(summary.count == 0);
    CHECK(summary.max == 0.0);
}

TEST_CASE("Summaries use nearest-rank percentiles", "[metrics]") {
    std::vector<double> samples;
    for (int i = 1; i <= 100; ++i) {
        samples.push_back(i);
    }
    const auto summary = summarise(samples);
    CHECK(summary.count == 100);
    CHECK(summary.mean == Approx(50.5));
    CHECK(summary.min == 1.0);
    CHECK(summary.p50 == 50.0);
    CHECK(summary.p95 == 95.0);
    CHECK(summary.p99 == 99.0);
    CHECK(summary.max == 100.0);
}

TEST_CASE("A steady 50 Hz stream has a 20 ms interval and no spread", "[metrics]") {
    const auto summary = summarise(std::vector<double>(250, 20.0));
    CHECK(summary.mean == Approx(20.0));
    CHECK(summary.stddev == Approx(0.0).margin(1e-12));
}

TEST_CASE("Draining a recorder empties it", "[metrics]") {
    IntervalRecorder recorder;
    recorder.record(1.0);
    recorder.record(3.0);
    CHECK(recorder.drain().count == 2);
    CHECK(recorder.drain().count == 0);
}

TEST_CASE("A thread stat line yields its name and CPU ticks", "[metrics][cpu]") {
    const auto stat = parse_thread_stat(
        "1234 (bbm-video) S 1 1234 1234 0 -1 4194560 100 0 0 0 250 75 0 0 20 0 9 0 1 2 3");
    REQUIRE(stat.has_value());
    CHECK(stat->name == "bbm-video");
    CHECK(stat->cpu_ticks == 325);
}

TEST_CASE("A thread name may contain spaces and parentheses", "[metrics][cpu]") {
    const auto stat =
        parse_thread_stat("7 (a (b) c) R 1 7 7 0 -1 0 0 0 0 0 10 5 0 0 20 0 1 0 1 2 3");
    REQUIRE(stat.has_value());
    CHECK(stat->name == "a (b) c");
    CHECK(stat->cpu_ticks == 15);
}

TEST_CASE("A truncated or malformed stat line is rejected", "[metrics][cpu]") {
    CHECK_FALSE(parse_thread_stat("no parentheses here").has_value());
    CHECK_FALSE(parse_thread_stat("1 (x) S 1 2 3").has_value());
    CHECK_FALSE(parse_thread_stat("1 (x) S 1 1 1 0 -1 0 0 0 0 0 abc 5").has_value());
}

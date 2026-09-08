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

// Tests for CoprocessorClock: the exact, drift-free rational conversion from
// host time to coprocessor cycles shared by every Coprocessor implementation.
//
// Note on the non-monotonic assertion: the contract says a run_until (and so
// cycles_due) with a smaller host time than the previous call, without a
// rebase, is a contract violation asserted in debug builds. The project uses
// plain assert(), which aborts rather than throws, and the acceptance build
// is Release (NDEBUG), where assert() compiles out. There is no throwing-assert
// pattern in the project, so this file does not death-test the assertion; it
// verifies the positive contract instead (rebase then a smaller time is
// accepted and returns zero).

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorClock.hpp>

#include <cstdint>
#include <vector>

using namespace beebium;

// ===========================================================================
// Exactness: cumulative cycles equal floor(N * num / den)
// ===========================================================================

TEST_CASE("CoprocessorClock: cumulative equals floor(N*num/den) over a million host cycles",
          "[coprocessor][clock]") {
    struct Case { uint32_t num; uint32_t den; };
    const std::vector<Case> cases = {
        {3, 2},          // 65C02 second processor
        {5, 1},
        {3, 1},
        {1, 1},
        {6, 4},          // must behave exactly as 3/2
        {10000, 6667},   // unreduced, large, non-integer
    };

    const uint64_t N = 1'000'000;
    for (const auto& c : cases) {
        CoprocessorClock clock(ClockRatio{c.num, c.den});
        clock.cycles_due(0);   // establish the origin at host time 0
        uint64_t total = 0;
        for (uint64_t t = 1; t <= N; ++t) {
            total += clock.cycles_due(t);
        }
        const uint64_t expected =
            static_cast<uint64_t>(N) * c.num / c.den;
        INFO("ratio " << c.num << "/" << c.den);
        CHECK(total == expected);
    }
}

TEST_CASE("CoprocessorClock: a single large jump equals floor(t*num/den)",
          "[coprocessor][clock]") {
    // The remainder-carry arithmetic must be overflow-safe for large spans:
    // one jump of many host cycles yields the same cumulative as stepping.
    CoprocessorClock clock(ClockRatio{3, 2});
    clock.cycles_due(0);   // establish the origin at host time 0
    const uint64_t t = 4'000'000'000ULL;  // > 2^31, exercises the wide path
    CHECK(clock.cycles_due(t) == t * 3 / 2);
}

// ===========================================================================
// Remainder carry: per-call sequences
// ===========================================================================

TEST_CASE("CoprocessorClock: 3/2 yields the 1,2,1,2 per-call sequence",
          "[coprocessor][clock]") {
    CoprocessorClock clock(ClockRatio{3, 2});
    clock.cycles_due(0);   // establish the origin at host time 0
    std::vector<uint64_t> seq;
    for (uint64_t t = 1; t <= 8; ++t) {
        seq.push_back(clock.cycles_due(t));
    }
    CHECK(seq == std::vector<uint64_t>{1, 2, 1, 2, 1, 2, 1, 2});
}

TEST_CASE("CoprocessorClock: 6/4 yields the same 1,2,1,2 sequence as 3/2",
          "[coprocessor][clock]") {
    CoprocessorClock clock(ClockRatio{6, 4});
    clock.cycles_due(0);   // establish the origin at host time 0
    std::vector<uint64_t> seq;
    for (uint64_t t = 1; t <= 8; ++t) {
        seq.push_back(clock.cycles_due(t));
    }
    CHECK(seq == std::vector<uint64_t>{1, 2, 1, 2, 1, 2, 1, 2});
}

TEST_CASE("CoprocessorClock: integer ratios yield a constant per-call count",
          "[coprocessor][clock]") {
    SECTION("5/1 yields 5 every call") {
        CoprocessorClock clock(ClockRatio{5, 1});
        clock.cycles_due(0);
        for (uint64_t t = 1; t <= 8; ++t) {
            CHECK(clock.cycles_due(t) == 5);
        }
    }
    SECTION("3/1 yields 3 every call") {
        CoprocessorClock clock(ClockRatio{3, 1});
        clock.cycles_due(0);
        for (uint64_t t = 1; t <= 8; ++t) {
            CHECK(clock.cycles_due(t) == 3);
        }
    }
    SECTION("1/1 yields 1 every call") {
        CoprocessorClock clock(ClockRatio{1, 1});
        clock.cycles_due(0);
        for (uint64_t t = 1; t <= 8; ++t) {
            CHECK(clock.cycles_due(t) == 1);
        }
    }
}

// ===========================================================================
// Repeated calls at the same host time run nothing
// ===========================================================================

TEST_CASE("CoprocessorClock: repeated call at same host time returns zero",
          "[coprocessor][clock]") {
    CoprocessorClock clock(ClockRatio{3, 2});
    clock.cycles_due(0);   // establish the origin at host time 0
    CHECK(clock.cycles_due(1) == 1);
    CHECK(clock.cycles_due(1) == 0);
    CHECK(clock.cycles_due(1) == 0);
    // The carried remainder is intact: advancing continues the sequence.
    CHECK(clock.cycles_due(2) == 2);
    CHECK(clock.cycles_due(2) == 0);
    CHECK(clock.cycles_due(3) == 1);
}

// ===========================================================================
// Reset / rebase: origin discarded, host time may go backwards
// ===========================================================================

TEST_CASE("CoprocessorClock: rebase then a smaller host time is accepted and returns zero",
          "[coprocessor][clock]") {
    CoprocessorClock clock(ClockRatio{3, 2});
    // Advance well into a session.
    for (uint64_t t = 1; t <= 1000; ++t) {
        clock.cycles_due(t);
    }

    clock.rebase();

    // A hard reset zeroes the host cycle count, so the next call has a much
    // smaller host time. It defines the new origin and runs nothing.
    CHECK(clock.cycles_due(0) == 0);

    // Subsequent calls run from the new origin, resuming the 1,2,1,2 pattern.
    CHECK(clock.cycles_due(1) == 1);
    CHECK(clock.cycles_due(2) == 2);
    CHECK(clock.cycles_due(3) == 1);
}

TEST_CASE("CoprocessorClock: rebase to a nonzero origin has zero cycles due at the origin",
          "[coprocessor][clock]") {
    CoprocessorClock clock(ClockRatio{3, 2});
    clock.rebase();
    CHECK(clock.cycles_due(500) == 0);   // origin is 500, no catch-up burst
    CHECK(clock.cycles_due(501) == 1);
    CHECK(clock.cycles_due(502) == 2);
}

TEST_CASE("CoprocessorClock: a freshly constructed clock has no origin until the first call",
          "[coprocessor][clock]") {
    // A clock installed into a machine that has been running for a while must
    // not run a catch-up burst from host time zero. The time base is undefined
    // at construction, exactly as after rebase(): the first cycles_due() defines
    // the origin and returns zero, whatever the host time.
    CoprocessorClock clock(ClockRatio{3, 2});
    CHECK(clock.cycles_due(500) == 0);   // origin is 500, no billions-of-cycles burst
    CHECK(clock.cycles_due(501) == 1);
    CHECK(clock.cycles_due(502) == 2);
}

// ===========================================================================
// ratio() accessor
// ===========================================================================

TEST_CASE("CoprocessorClock: ratio() returns the constructed ratio",
          "[coprocessor][clock]") {
    CoprocessorClock clock(ClockRatio{3, 2});
    CHECK(clock.ratio().numerator == 3);
    CHECK(clock.ratio().denominator == 2);
}

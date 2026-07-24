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

// Tests for the deficit-based PacingController.
//
// The controller outputs cycles-per-tick based on how many cycles are
// "owed" at the current wall-clock time. Tests simulate tick sequences
// with I/O bursts (shortened tick periods) and verify correct average rate.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <beebium/PacingClock.hpp>
#include <beebium/PacingController.hpp>

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

using namespace beebium;
using Catch::Matchers::WithinAbs;

namespace {

struct TickResult {
    int64_t wall_ns;
    uint64_t cycles;
    uint32_t cycles_this_tick;
    double deficit;
};

/// Simulate ticks with optional I/O burst (shortened tick periods).
std::vector<TickResult> simulate(
    PacingController& ctrl,
    int num_ticks,
    int io_burst_start = -1,
    int io_burst_end = -1,
    int64_t base_interval_ns = 500'000,
    int64_t io_interval_ns = 200'000)
{
    std::vector<TickResult> results;
    int64_t wall_ns = 0;
    uint64_t total_cycles = 0;

    for (int i = 0; i < num_ticks; i++) {
        bool io = (i >= io_burst_start && i < io_burst_end);

        uint32_t cycles = ctrl.update(wall_ns, total_cycles);
        total_cycles += cycles;
        wall_ns += io ? io_interval_ns : base_interval_ns;

        results.push_back({wall_ns, total_cycles, cycles,
                            ctrl.last_deficit()});
    }
    return results;
}

double average_clock_rate(const std::vector<TickResult>& results,
                          int from_tick, int to_tick) {
    if (from_tick >= to_tick || to_tick > static_cast<int>(results.size()))
        return 0;
    auto& first = results[static_cast<size_t>(from_tick)];
    auto& last = results[static_cast<size_t>(to_tick - 1)];
    double wall_secs = static_cast<double>(last.wall_ns - first.wall_ns) / 1e9;
    double cycles = static_cast<double>(last.cycles - first.cycles);
    return wall_secs > 0 ? cycles / wall_secs : 0;
}

} // namespace


TEST_CASE("PacingController steady state matches target rate", "[pacing]") {
    PacingController ctrl(2'000'000, 500'000);

    auto results = simulate(ctrl, 1000);

    double rate = average_clock_rate(results, 500, 1000);
    REQUIRE_THAT(rate, WithinAbs(2'000'000, 10'000));

    // Cycles per tick should be near nominal (1,000 at 2MHz/500us) in steady state
    REQUIRE(results.back().cycles_this_tick >= 950);
    REQUIRE(results.back().cycles_this_tick <= 1'050);
}


TEST_CASE("PacingController adapts to shortened I/O tick", "[pacing]") {
    PacingController ctrl(2'000'000, 500'000);

    auto results = simulate(ctrl, 1000, 100, 150);

    SECTION("cycles reduce during burst") {
        // During burst: 200us tick at 2 MHz = 400 cycles target
        // After controller reacts (a few ticks), cycles should be small
        REQUIRE(results[105].cycles_this_tick <= 500);
    }

    SECTION("burst rate is close to target") {
        double rate = average_clock_rate(results, 110, 150);
        REQUIRE_THAT(rate, WithinAbs(2'000'000, 500'000));
    }

    SECTION("recovery is fast") {
        // After burst ends, should return to ~1,000 within a few ticks
        REQUIRE(results[160].cycles_this_tick >= 800);
    }

    SECTION("overall rate matches target") {
        double rate = average_clock_rate(results, 0, 1000);
        REQUIRE_THAT(rate, WithinAbs(2'000'000, 50'000));
    }
}


TEST_CASE("PacingController handles repeated short bursts", "[pacing]") {
    PacingController ctrl(2'000'000, 500'000);

    int num_ticks = 2000;
    int64_t wall_ns = 0;
    uint64_t total_cycles = 0;

    for (int i = 0; i < num_ticks; i++) {
        bool io = (i % 15) >= 10;
        uint32_t cycles = ctrl.update(wall_ns, total_cycles);
        total_cycles += cycles;
        wall_ns += io ? 200'000 : 500'000;
    }

    double wall_secs = static_cast<double>(wall_ns) / 1e9;
    double rate = static_cast<double>(total_cycles) / wall_secs;
    REQUIRE_THAT(rate, WithinAbs(2'000'000, 100'000));
}


TEST_CASE("PacingController 3 MHz parasite", "[pacing]") {
    PacingController ctrl(3'000'000, 500'000);

    auto results = simulate(ctrl, 1000, 100, 150);

    double rate = average_clock_rate(results, 200, 1000);
    REQUIRE_THAT(rate, WithinAbs(3'000'000, 100'000));

    // Base cycles should be 1,500 at 3MHz/500us in steady state
    REQUIRE(results.back().cycles_this_tick >= 1'400);
    REQUIRE(results.back().cycles_this_tick <= 1'600);
}


TEST_CASE("PacingController recovery from burst is bounded", "[pacing]") {
    PacingController ctrl(2'000'000, 500'000);

    // Long burst: 200 ticks at 200us
    auto results = simulate(ctrl, 500, 50, 250);

    SECTION("first burst tick overshoots") {
        // First tick runs nominal cycles (deficit = 1,000 before burst)
        REQUIRE(results[50].cycles_this_tick >= 900);
    }

    SECTION("subsequent burst ticks are smaller") {
        // After controller reacts, cycles should reduce
        REQUIRE(results[55].cycles_this_tick <= 500);
    }

    SECTION("deficit after burst is bounded") {
        REQUIRE(std::abs(results[260].deficit) < 5'000);
    }

    SECTION("recovery completes") {
        // After burst, cycles should return to near nominal
        REQUIRE(results[260].cycles_this_tick >= 500);
    }
}

TEST_CASE("estimate_max_speed_multiplier extrapolates from active fraction",
          "[pacing]") {
    // Running real-time but computing only half the wall clock -> could double.
    REQUIRE_THAT(estimate_max_speed_multiplier(1.0, 0.5), WithinAbs(2.0, 1e-9));
    // 8% active at 1x -> ~12.5x headroom.
    REQUIRE_THAT(estimate_max_speed_multiplier(1.0, 0.08), WithinAbs(12.5, 1e-9));
    // Already running at 4x while computing a quarter of the time -> 16x.
    REQUIRE_THAT(estimate_max_speed_multiplier(4.0, 0.25), WithinAbs(16.0, 1e-9));
}

TEST_CASE("estimate_max_speed_multiplier returns achieved when saturated",
          "[pacing]") {
    // Fully busy (active_fraction == 1): the ceiling is the achieved rate.
    REQUIRE_THAT(estimate_max_speed_multiplier(12.0, 1.0), WithinAbs(12.0, 1e-9));
    // active_fraction above 1 (measurement noise) is clamped, never below achieved.
    REQUIRE_THAT(estimate_max_speed_multiplier(12.0, 1.5), WithinAbs(12.0, 1e-9));
}

TEST_CASE("estimate_max_speed_multiplier reports no estimate when idle",
          "[pacing]") {
    // Nothing to extrapolate from: paused/idle window or zero achieved rate.
    REQUIRE(estimate_max_speed_multiplier(1.0, 0.0) == 0.0);
    REQUIRE(estimate_max_speed_multiplier(0.0, 0.5) == 0.0);
    REQUIRE(estimate_max_speed_multiplier(-1.0, 0.5) == 0.0);
}

TEST_CASE("PacingController catch-up clamp scales with speed", "[pacing]") {
    // 2 MHz, 500us quantum -> nominal 1000 cycles, 1x ceiling max_cycles 3000.
    PacingController ctrl(2'000'000, 500'000);

    // A 1s elapsed against 0 cycles is a deficit far above the clamp, so the
    // return is the (speed-scaled) ceiling.
    SECTION("1x is clamped to the nominal ceiling") {
        REQUIRE(ctrl.update(1'000'000'000, 0, 1.0) == 3000);
    }
    SECTION("S>1 raises the ceiling proportionally") {
        // Without this, paced speed could never exceed max_ratio (~3x) however
        // high the request -- the regression this guards against.
        REQUIRE(ctrl.update(1'000'000'000, 0, 4.0) == 12000);
    }
    SECTION("S<1 does not shrink the ceiling below the 1x headroom") {
        REQUIRE(ctrl.update(1'000'000'000, 0, 0.5) == 3000);
    }
    SECTION("default speed is 1x") {
        REQUIRE(ctrl.update(1'000'000'000, 0) == 3000);
    }
}

// A hard reset (debugger.reset) zeroes the machine's cycle counter while the
// pacing clock still holds the pre-reset total from its last report_cycles.
// rebase() must re-anchor the baseline to the machine's actual (post-reset)
// count: anchoring it to the stale, larger total makes the next
// total_cycles - baseline_cycles underflow, the controller reads the machine
// as wildly ahead, and it paces down to one cycle per tick -- a ~kHz crawl that
// leaves a reset machine unable to boot. rebase(current) takes the real count.
TEST_CASE("PacingClock rebase after a reset does not crawl", "[pacing]") {
    PacingConfig config{2'000'000, 500, 1.0};  // 2 MHz, real-time
    PacingClock clock(config, std::chrono::microseconds(500), PlatformSleep{});

    // A long session has elapsed: the clock last saw a large cycle total.
    clock.report_cycles(600'000'000);

    // A hard reset zeroes the machine's counter; rebase is handed that count.
    clock.rebase(10);

    // Wall time passes while the reset machine has barely advanced (behind
    // real time), so the controller should ask for a healthy catch-up.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    clock.report_cycles(110);

    uint64_t next = clock.cycles_for_next_tick();

    // With the stale baseline the difference underflows and the controller
    // returns its floor of 1; the fix yields the real deficit, clamped to the
    // controller's ceiling (nominal 1000 * 3 = 3000 at 1x).
    REQUIRE(next >= 1000);
}

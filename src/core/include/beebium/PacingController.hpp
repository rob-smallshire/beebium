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

#pragma once

#include <algorithm>
#include <cstdint>

namespace beebium {

/// Deficit-based pacing controller.
///
/// Computes how many cycles to execute by calculating the deficit between
/// target cumulative cycles (from wall-clock time) and actual cumulative
/// cycles. The result naturally adapts to variable tick periods:
///
/// - Normal tick (500us quantum): deficit ≈ 1,000 cycles (at 2 MHz)
/// - After OS sleep overshoot (1.5ms): deficit ≈ 3,000 (catches up)
/// - After running ahead: deficit ≈ 0 (pauses until wall-clock catches up)
///
/// The max_cycles clamp prevents concentrated catch-up after deficits,
/// spreading recovery across many ticks imperceptibly. It scales with the
/// speed multiplier so that requesting S>1 is not itself clamped down to the
/// real-time ceiling.
///
/// Pure computation -- no threading, no clocks, no side effects.
///
class PacingController {
public:
    /// The longest tick span the catch-up ceiling covers.
    static constexpr int64_t kMaxSpanNs = 50'000'000;  // 50 ms

    /// @param target_clock_hz  Target crystal frequency (e.g., 2000000)
    /// @param quantum_ns       Sleep quantum in nanoseconds (from measurement)
    /// @param max_ratio        Maximum cycles as multiple of nominal per-quantum
    ///                         cycles. 1.015 = allow 1.5% overshoot for smooth
    ///                         catch-up after deficits.
    PacingController(uint64_t target_clock_hz, int64_t quantum_ns,
                     double max_ratio = 3.0)
        : target_clock_hz_(target_clock_hz)
        , nominal_cycles_(static_cast<uint32_t>(
              static_cast<double>(target_clock_hz) * quantum_ns / 1e9))
        , max_cycles_(static_cast<uint32_t>(nominal_cycles_ * max_ratio))
        , max_ratio_(max_ratio) {}

    /// Compute cycles to execute in the next tick.
    ///
    /// @param wall_elapsed_ns   Wall-clock nanoseconds since start/reset (for
    ///                          variable-rate pacing, multiplier-scaled time).
    /// @param total_cycles      Total crystal ticks executed since start/reset.
    /// @param speed_multiplier  Current effective speed. The per-tick catch-up
    ///                          ceiling is the nominal headroom times this, so a
    ///                          steady S>1 rate isn't clamped down to the 1x
    ///                          ceiling (which would cap paced speed at max_ratio
    ///                          regardless of request). Defaults to 1.0.
    /// @param since_last_ns     Wall nanoseconds since the previous tick. The
    ///                          catch-up ceiling covers at least this span (up
    ///                          to kMaxSpanNs), so late ticks keep real time.
    /// @return Number of cycles to execute. Zero if ahead of real-time.
    uint32_t update(int64_t wall_elapsed_ns, uint64_t total_cycles,
                    double speed_multiplier = 1.0, int64_t since_last_ns = 0) {
        double target = (static_cast<double>(wall_elapsed_ns) / 1e9)
                      * target_clock_hz_;
        double deficit = target - static_cast<double>(total_cycles);

        last_deficit_ = deficit;
        ++tick_count_;

        if (deficit <= 0.0) return 1;  // Always run at least 1 cycle
        double max_cycles = static_cast<double>(max_cycles_)
                          * std::max(1.0, speed_multiplier);
        // A late tick spans more wall time than a quantum, so its ceiling
        // spans that time too; otherwise ticks arriving more than max_ratio
        // quanta apart cap the machine below real time with the host idle
        // (#241). The span is clamped so that one late tick never runs long
        // enough to delay a debugger or quiesce request noticeably.
        const double span_ns = std::min(static_cast<double>(since_last_ns),
                                        static_cast<double>(kMaxSpanNs));
        double span_cycles = max_ratio_ * static_cast<double>(target_clock_hz_)
                           * std::max(1.0, speed_multiplier) * span_ns / 1e9;
        max_cycles = std::max(max_cycles, span_cycles);
        if (deficit > max_cycles) return static_cast<uint32_t>(max_cycles);
        return static_cast<uint32_t>(deficit);
    }

    void reset() {
        last_deficit_ = 0.0;
        tick_count_ = 0;
    }

    // --- Monitoring ---
    double last_deficit() const { return last_deficit_; }
    uint64_t tick_count() const { return tick_count_; }
    uint32_t nominal_cycles() const { return nominal_cycles_; }
    uint32_t max_cycles() const { return max_cycles_; }

private:
    uint64_t target_clock_hz_;
    uint32_t nominal_cycles_;   // cycles per quantum at 1x speed
    uint32_t max_cycles_;       // ceiling per tick
    double max_ratio_;

    double last_deficit_ = 0.0;
    uint64_t tick_count_ = 0;
};

} // namespace beebium

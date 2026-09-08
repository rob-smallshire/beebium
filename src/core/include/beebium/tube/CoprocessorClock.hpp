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

#include "Coprocessor.hpp"

#include <cassert>
#include <cstdint>

namespace beebium {

// Converts host time to coprocessor cycles for a fixed ClockRatio, exactly
// and without drift. Every coprocessor implementation can share this rather
// than reimplementing the rational arithmetic.
//
// The origin t0 is host time zero at construction. cycles_due(t) returns the
// coprocessor cycles that became due since the previous call, so that the
// cumulative count after any sequence of calls reaching host time t equals
// exactly floor((t - t0) * numerator / denominator). The fractional remainder
// carries across calls, so over any interval the coprocessor runs exactly the
// cycles the ratio dictates -- never one more or one fewer.
class CoprocessorClock {
public:
    explicit CoprocessorClock(ClockRatio ratio)
        : ratio_(ratio) {
        assert(ratio.numerator > 0 && ratio.denominator > 0);
    }

    // Discard the time base: the next cycles_due() call defines the origin
    // and returns zero. Used across a hardware reset, where host time may
    // jump backwards.
    void rebase() {
        have_origin_ = false;
        remainder_ = 0;
    }

    // Number of coprocessor cycles that became due since the previous call,
    // for the given host time. Exact, carries the remainder, never drifts.
    // Asserts host_cycle is not less than the previous host_cycle unless a
    // rebase() (or construction) has reset the origin.
    uint64_t cycles_due(uint64_t host_cycle) {
        if (!have_origin_) {
            have_origin_ = true;
            last_host_cycle_ = host_cycle;
            remainder_ = 0;
            return 0;
        }
        assert(host_cycle >= last_host_cycle_);
        const uint64_t delta = host_cycle - last_host_cycle_;
        last_host_cycle_ = host_cycle;

        // cycles = floor((remainder_ + delta * num) / den), carrying the new
        // remainder. Split delta by den first so no intermediate overflows for
        // delta < 2^62 with num, den < 2^16: the whole-quotient term q * num
        // never exceeds the true cumulative result (which the caller keeps in
        // uint64), and the fractional term stays below 2^33.
        const uint64_t num = ratio_.numerator;
        const uint64_t den = ratio_.denominator;
        const uint64_t q = delta / den;
        const uint64_t r = delta % den;
        const uint64_t partial = remainder_ + r * num;  // < den + den * num
        remainder_ = partial % den;
        return q * num + partial / den;
    }

    ClockRatio ratio() const { return ratio_; }

private:
    ClockRatio ratio_;
    uint64_t last_host_cycle_ = 0;
    uint64_t remainder_ = 0;   // fractional cycles carried, always < denominator
    bool have_origin_ = true;  // origin is host time zero at construction
};

}  // namespace beebium

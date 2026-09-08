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

#include <cstdint>

namespace beebium {

// Exact clock ratio: coprocessor cycles per host cycle, as a rational.
// The 65C02 second processor runs at 3 MHz against a 2 MHz host, so 3/2.
struct ClockRatio {
    uint32_t numerator;    // coprocessor cycles
    uint32_t denominator;  // per this many host cycles
};

// A Tube coprocessor: everything on the far side of the Tube cable, driven
// by the host's clock. Supplied by an extension, installed in the TubeSocket
// while attached. Replaces the earlier ParasiteTickable, moving from a
// per-cycle tick() driven by a socket-owned phase accumulator to a
// run_until() driven by host time, with the clock ratio owned by the
// coprocessor itself.
//
// The host and the coprocessor are independent clock domains that meet only
// at the Tube ULA registers and its interrupt outputs. The socket drives the
// coprocessor in host time; the coprocessor converts host time to its own
// cycles via its ClockRatio (see CoprocessorClock).
class Coprocessor {
public:
    virtual ~Coprocessor() = default;

    // Execute every coprocessor cycle due at or before host_cycle that has
    // not yet executed. Returns when the coprocessor's clock has reached the
    // point equivalent to host_cycle.
    //
    // Between resets, successive host_cycle arguments are non-decreasing; a
    // smaller value is a contract violation asserted in debug builds. Calling
    // with the same value more than once runs nothing on the second call.
    virtual void run_until(uint64_t host_cycle) = 0;

    // True while the debugger has stopped this coprocessor. While paused,
    // run_until still advances the record of host time but runs no cycles;
    // cycles that fall in a paused interval are lost, not deferred.
    virtual bool is_paused() const = 0;

    // Hardware reset, propagated from the host's reset line through the Tube
    // cable. Restarts the CPU at its reset vector and discards the time base:
    // the next run_until(t) establishes t as the new origin with zero cycles
    // due. This is required because a hard host reset zeroes the host cycle
    // count, so host time legitimately goes backwards across a reset.
    virtual void reset() = 0;

    // Exact clock ratio, coprocessor cycles per host cycle.
    virtual ClockRatio clock_ratio() const = 0;

    // Diagnostic: current parasite PC, or 0xFFFF if not applicable.
    virtual uint16_t diag_pc() const { return 0xFFFF; }
};

}  // namespace beebium

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

// Exact clock ratio: units per host cycle, as a rational. For a board whose
// clock is counted in crystal ticks (see BoardTiming) this is ticks per host
// cycle -- 12 MHz against a 2 MHz host is 6/1 for the 3 MHz second processor.
struct ClockRatio {
    uint32_t numerator;    // coprocessor ticks (or cycles)
    uint32_t denominator;  // per this many host cycles
};

// Per-board timing for a 6502-family coprocessor, in crystal ticks (issue #70,
// docs/discussion/tube-coprocessor-board-timing.md). The board's clock is
// counted in crystal periods, not CPU cycles, because two effects make a CPU
// cycle no longer one fixed length: a write cycle is stretched by one tick, and
// a DRAM refresh steals one cycle at the next opcode fetch periodically.
//
//   3 MHz 6502 Second Processor (cheese wedge):  {6,1}, 4, 5, 176, 1
//   4 MHz 65C102 Co-processor (internal):        {2,1}, 1, 1,  64, 1
//   256K Turbo:                                  refresh_period_ticks = 0
//                                                (its refresh is unmeasured)
//
// A board with unit ticks and no refresh -- {ratio, 1, 1, 0, 1} -- is exactly a
// plain CPU-cycle clock at that ratio, with no board effects.
struct BoardTiming {
    ClockRatio ticks_per_host_cycle;  // crystal ticks per 2 MHz host cycle
    uint32_t read_cycle_ticks;        // ticks a read cycle costs
    uint32_t write_cycle_ticks;       // ticks a write cycle costs (>= read)
    uint32_t refresh_period_ticks;    // ticks between refreshes; 0 = no refresh
    uint32_t refresh_hold_cycles;     // cycles the CPU is held for a refresh
};

// A Tube coprocessor: everything on the far side of the Tube cable, driven
// by the host's clock. Supplied by an extension, installed in the TubeSocket
// while attached.
//
// The host and the coprocessor are independent clock domains that meet only
// at the Tube ULA registers and its interrupt outputs. The socket drives the
// coprocessor in host time; the coprocessor converts host time to its own
// clock (crystal ticks) via its BoardTiming (see CoprocessorClock).
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

    // Debugger stop and resume, and the current state. While paused,
    // run_until still advances the record of host time but runs no cycles;
    // cycles that fall in a paused interval are lost, not deferred. These are
    // on the interface because the debugger's cross-processor stop logic uses
    // them: the server pauses the coprocessor when a host breakpoint with
    // stop_counterpart fires.
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual bool is_paused() const = 0;

    // Hardware reset, propagated from the host's reset line through the Tube
    // cable. Restarts the CPU at its reset vector and discards the time base:
    // the next run_until(t) establishes t as the new origin with zero cycles
    // due. This is required because a hard host reset zeroes the host cycle
    // count, so host time legitimately goes backwards across a reset.
    virtual void reset() = 0;

    // Per-board timing (crystal ticks per host cycle, cycle costs, refresh).
    virtual BoardTiming board_timing() const = 0;
};

}  // namespace beebium

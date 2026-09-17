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

#include <beebium/tube/CoprocessorRunner.hpp>

#include <algorithm>

namespace beebium {

CoprocessorRunner::CoprocessorRunner(TubeCoprocessorBackend& backend, std::span<const uint8_t, 4096> rom,
                               BoardTiming timing)
    : tube_port_(backend)
    , memory_(tube_port_, rom)
    , cpu_(memory_, tube_port_)
    , timing_(timing)
    , clock_(timing.ticks_per_host_cycle)
{
    std::copy(rom.begin(), rom.end(), rom_.begin());
}

void CoprocessorRunner::reset() {
    cpu_.reset();
    // Discard the clock's time base: a hard host reset zeroes the host cycle
    // count, so the next run_until() must establish a fresh origin rather than
    // treat the reset as host time running backwards.
    clock_.rebase();
    // The refresh request flip-flop is cleared by NRST via IC6, and the tick
    // budget starts empty (docs/discussion/tube-coprocessor-board-timing.md).
    tick_budget_ = 0;
    refresh_timer_ = 0;
    refresh_pending_ = false;
}

void CoprocessorRunner::run_until(uint64_t host_cycle) {
    // Advance the clock's record of host time and learn how many crystal ticks
    // have become due. Do this even while paused: the paused interval's ticks
    // are lost, not deferred, so a resumed coprocessor does not catch up.
    const uint64_t due_ticks = clock_.cycles_due(host_cycle);
    if (paused_) return;
    tick_budget_ += static_cast<int64_t>(due_ticks);

    // Spend the tick budget cycle by cycle. A coprocessor cycle is no longer one
    // fixed length: a read costs read_cycle_ticks, a write write_cycle_ticks, and
    // a DRAM refresh holds the CPU for one cycle at the next opcode fetch once the
    // refresh timer reaches its period. See the design note (issue #70).
    const bool has_refresh = timing_.refresh_period_ticks != 0;
    while (true) {
        // A pending refresh lands on the next opcode fetch (SYNC): consume one
        // held cycle's worth of ticks, execute nothing, and reload the timer.
        if (has_refresh && refresh_pending_ && M6502_IsAboutToExecute(&cpu_.cpu())) {
            const int64_t hold =
                static_cast<int64_t>(timing_.refresh_hold_cycles) * timing_.read_cycle_ticks;
            if (tick_budget_ < hold) break;
            tick_budget_ -= hold;
            refresh_pending_ = false;
            refresh_timer_ = 0;  // stall-then-reload on the RAS edge
            ++refresh_hold_count_;
            continue;
        }

        // Only execute a cycle we can afford at least a read for; a write then
        // overspends by one tick, carried as a deficit into the next run_until.
        if (tick_budget_ < static_cast<int64_t>(timing_.read_cycle_ticks)) break;

        if (check_breakpoints()) break;  // paused at the breakpoint PC; don't execute it
        cpu_.tick();
        const uint32_t cost =
            cpu_.cpu().read ? timing_.read_cycle_ticks : timing_.write_cycle_ticks;
        tick_budget_ -= static_cast<int64_t>(cost);

        // The refresh timer counts executing ticks and stalls once it fires,
        // until the hold clears it, so the interval is period + the wait for SYNC.
        if (has_refresh && !refresh_pending_) {
            refresh_timer_ += cost;
            if (refresh_timer_ >= timing_.refresh_period_ticks) refresh_pending_ = true;
        }

        check_watchpoints();
        ++sequence_;
    }
}

bool CoprocessorRunner::check_breakpoints() {
    // Check breakpoints before tick(), when register updates are complete and
    // the CPU is about to decode the next opcode.
    if (breakpoint_entries_.empty() || !M6502_IsAboutToExecute(&cpu_.cpu())) {
        return false;
    }
    const uint16_t pc = cpu_.cpu().opcode_pc.w;
    for (auto& bp : breakpoint_entries_) {
        if (bp.start > pc) break;
        if (bp.matches(pc)) {
            if (on_breakpoint_hit_) on_breakpoint_hit_(bp, pc);
            if (paused_) return true;
        }
    }
    return false;
}

bool CoprocessorRunner::check_watchpoints() {
    // Watchpoint check (every bus access, after tick).
    if (watchpoint_entries_.empty()) return false;
    const uint16_t addr = cpu_.cpu().abus.w;
    const bool is_write = !cpu_.cpu().read;
    for (const auto& wp : watchpoint_entries_) {
        if (wp.start > addr) break;
        if (wp.matches(addr, is_write)) {
            if (on_watchpoint_hit_) {
                on_watchpoint_hit_(wp, addr, cpu_.cpu().dbus, is_write);
            }
            return true;
        }
    }
    return false;
}

void CoprocessorRunner::run(uint64_t cycles) {
    uint64_t batch_end = cpu_.cycle_count() + cycles;

    while (cpu_.cycle_count() < batch_end) {
        if (paused_) return;
        if (check_breakpoints()) return;
        cpu_.tick();
        if (check_watchpoints()) return;
    }
}

uint64_t CoprocessorRunner::step_instruction() {
    return cpu_.step_instruction();
}

void CoprocessorRunner::step() {
    // step() is the LIVE execution path: the coprocessor is ticked single-threaded
    // from Machine::step() via TubeSocket::tick_coprocessor() -> tick() -> step().
    // run() is never called outside tests, so the breakpoint and watchpoint
    // checks must happen here -- otherwise coprocessor breakpoints never fire during
    // normal execution.
    if (check_breakpoints()) return;  // paused at the breakpoint PC; don't execute it
    cpu_.tick();
    check_watchpoints();
    ++sequence_;
}

void CoprocessorRunner::pause() {
    paused_ = true;
    ++sequence_;
}

void CoprocessorRunner::resume() {
    paused_ = false;
    ++sequence_;
}

}  // namespace beebium

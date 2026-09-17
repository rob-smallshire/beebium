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

// A pending DRAM refresh lands on the next opcode fetch (SYNC): consume one
// held cycle's worth of ticks, execute nothing, clear the request and reload
// the timer (the stall-then-reload on the RAS edge). Returns whether it fired.
// Charges the tick budget unconditionally; the caller decides affordability.
bool CoprocessorRunner::refresh_hold_if_due() {
    if (timing_.refresh_period_ticks == 0 || !refresh_pending_ ||
        !M6502_IsAboutToExecute(&cpu_.cpu())) {
        return false;
    }
    tick_budget_ -=
        static_cast<int64_t>(timing_.refresh_hold_cycles) * timing_.read_cycle_ticks;
    refresh_pending_ = false;
    refresh_timer_ = 0;
    ++refresh_hold_count_;
    return true;
}

// Execute one CPU cycle: breakpoint check (stop before executing if it pauses),
// tick, charge read_cycle_ticks or write_cycle_ticks by the cycle's R/W, advance
// the refresh timer, watchpoint check. Returns false if a breakpoint paused
// before the cycle ran. Charges the tick budget unconditionally.
bool CoprocessorRunner::execute_one_cycle() {
    if (check_breakpoints()) return false;
    cpu_.tick();
    const uint32_t cost =
        cpu_.cpu().read ? timing_.read_cycle_ticks : timing_.write_cycle_ticks;
    tick_budget_ -= static_cast<int64_t>(cost);
    // The refresh timer counts executing ticks and stalls once it fires, until
    // the hold clears it, so the interval is the period plus the wait for SYNC.
    if (timing_.refresh_period_ticks != 0 && !refresh_pending_) {
        refresh_timer_ += cost;
        if (refresh_timer_ >= timing_.refresh_period_ticks) refresh_pending_ = true;
    }
    check_watchpoints();
    ++sequence_;
    return true;
}

void CoprocessorRunner::run_until(uint64_t host_cycle) {
    // Advance the clock's record of host time and learn how many crystal ticks
    // have become due. Do this even while paused: the paused interval's ticks
    // are lost, not deferred, so a resumed coprocessor does not catch up.
    const uint64_t due_ticks = clock_.cycles_due(host_cycle);
    if (paused_) return;
    tick_budget_ += static_cast<int64_t>(due_ticks);

    // Spend the tick budget cycle by cycle, gating each action on affordability.
    // A coprocessor cycle is no longer one fixed length (issue #70): reads and
    // writes cost different ticks, and a refresh hold executes nothing.
    while (true) {
        if (timing_.refresh_period_ticks != 0 && refresh_pending_ &&
            M6502_IsAboutToExecute(&cpu_.cpu())) {
            const int64_t hold =
                static_cast<int64_t>(timing_.refresh_hold_cycles) * timing_.read_cycle_ticks;
            if (tick_budget_ < hold) break;
            refresh_hold_if_due();
            continue;
        }
        // Only execute a cycle we can afford at least a read for; a write then
        // overspends by one tick, carried as a deficit into the next run_until.
        if (tick_budget_ < static_cast<int64_t>(timing_.read_cycle_ticks)) break;
        if (!execute_one_cycle()) break;  // paused at the breakpoint PC
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
    // Loop the single step until the CPU is about to execute again after at least
    // one executed cycle, going through the same breakpoint/watchpoint and refresh
    // charging as run_until (the old direct cpu_.step_instruction() bypassed all
    // of them). A leading refresh hold executes no cycle, so it is charged but not
    // counted; the return is CPU cycles executed. A breakpoint that pauses stops it.
    const uint64_t start = cpu_.cycle_count();
    do {
        step();
        if (paused_) break;
    } while (cpu_.cycle_count() == start || !M6502_IsAboutToExecute(&cpu_.cpu()));
    return cpu_.cycle_count() - start;
}

void CoprocessorRunner::step() {
    // The debugger's single cycle step, on the same execution path as run_until:
    // a due refresh hold at an opcode fetch, otherwise one executed CPU cycle.
    // Both charge the tick budget (which may go negative here; run_until then
    // waits for host time to catch up), so a debugger-stepped coprocessor keeps
    // the board's timing rather than being a second, faster path.
    if (!refresh_hold_if_due()) execute_one_cycle();
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

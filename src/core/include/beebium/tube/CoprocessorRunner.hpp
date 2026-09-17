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
#include "CoprocessorClock.hpp"
#include "CoprocessorCpu.hpp"
#include "CoprocessorMemoryMap.hpp"
#include "TubeCoprocessorBackend.hpp"
#include "../Types.hpp"
#include "../Cpu6502Descriptor.hpp"
#include "beebium/extension/CpuDebugTarget.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace beebium {

// Coprocessor execution runner -- the emulation engine for a second processor.
//
// Owns the CPU, memory map, and Tube port, and provides an execution loop.
// This is the coprocessor's analogue of Machine<Hardware> on the host side.
//
// The runner implements the Coprocessor contract: it is driven in host time
// by TubeSocket::run_coprocessor_until(), converting host cycles to coprocessor
// cycles through a CoprocessorClock for its clock ratio. It supports
// pause/resume for debugger integration; while paused, run_until() advances
// the clock's record of host time but runs no cycles.
//
// This class is specific to the 6502 second processor family (3 MHz, ratio
// 3/2). Future coprocessors (6809, Z80, 80186, 32016) would have their own
// runner classes with different CPU and memory map types and clock ratios.

class CoprocessorRunner : public Coprocessor, public CpuDebugTarget {
public:
    using Memory = CoprocessorMemoryMap;
    using BreakpointHitCallback = std::function<void(const BreakpointEntry& bp, uint32_t pc)>;

    // Construct with an external coprocessor backend, the 4 KB ROM image (the
    // 2732 device contents, mapped at &F000-&FFFF), and the board timing (see
    // BoardTiming). The default is a plain 3:2 clock with no board effects
    // (unit ticks, no refresh) -- the pre-issue-70 behaviour, for callers that
    // do not model a specific board; the plugins pass their real BoardTiming.
    // The caller owns the backend and must keep it alive for the runner's life.
    CoprocessorRunner(TubeCoprocessorBackend& backend, std::span<const uint8_t, 4096> rom,
                   BoardTiming timing = BoardTiming{ClockRatio{3, 2}, 1, 1, 0, 1});
    ~CoprocessorRunner() = default;

    // Non-copyable (owns M6502 with internal pointers)
    CoprocessorRunner(const CoprocessorRunner&) = delete;
    CoprocessorRunner& operator=(const CoprocessorRunner&) = delete;

    // Reset CPU, memory map, and Tube port, and rebase the clock so the next
    // run_until() establishes a fresh time origin. Overrides Coprocessor::reset()
    // so TubeSocket can propagate the host's reset signal across the Tube cable
    // to the coprocessor. Required because a hard host reset zeroes the host cycle
    // count, so host time legitimately goes backwards across a reset.
    void reset() override;

    // Coprocessor::run_until() -- run every coprocessor cycle due at or before
    // host_cycle. While paused, advances the clock's host-time record but runs
    // nothing; those cycles are lost, not deferred.
    void run_until(uint64_t host_cycle) override;

    // Coprocessor::board_timing() -- the board's crystal-tick timing.
    BoardTiming board_timing() const override { return timing_; }

    // Execute for the given number of cycles, or until shutdown.
    // Checks pause state periodically.
    void run(uint64_t cycles);

    // Execute one complete instruction. Returns the number of cycles taken.
    uint64_t step_instruction() override;

    // Cycle counter (CPU cycles executed; a refresh hold executes none).
    uint64_t cycle_count() const override { return cpu_.cycle_count(); }

    // DRAM refresh holds performed so far (issue #70); for tests and diagnostics.
    uint64_t refresh_hold_count() const { return refresh_hold_count_; }

    // Unspent host ticks after the last run_until (issue #70). Bounded by one
    // cycle's cost: the loop stops when it cannot afford a read, and a write
    // overshoots by at most one tick, so the coprocessor never runs ahead of due
    // time by more than a cycle. For tests and diagnostics.
    int64_t tick_budget() const { return tick_budget_; }

    // --- Debugger pause/resume ---

    void pause() override;
    void resume() override;
    bool is_paused() const override { return paused_; }
    void prepare_for_step() override {} // Nothing to resync: the coprocessor is never bus-stretched

    // Wait until run() has exited after a pause (no-op in single-threaded mode).
    void wait_until_idle() override {}

    // The coprocessor executes only on the host emulation thread, which drives it
    // through TubeSocket::run_coprocessor_until(). To mutate its debug state
    // safely that thread must be idle, which means pausing the host emulation
    // loop. The runner has no handle to the host, so the server injects a
    // quiescer that does so (wired the same way as the cross-processor stop);
    // in a standalone test it is wired to the test's own Machine. When no
    // quiescer is set the runner is not being driven by another thread, so
    // running fn directly is safe.
    void set_execution_quiescer(
        std::function<void(const std::function<void()>&)> quiescer) override {
        execution_quiescer_ = std::move(quiescer);
    }
    void with_execution_stopped(const std::function<void()>& fn) override {
        if (execution_quiescer_) {
            execution_quiescer_(fn);
        } else {
            fn();
        }
    }

    // --- Sequence counter (increments on mutations, for change detection) ---

    uint64_t sequence() const override { return sequence_; }

    // --- Family-agnostic CPU description and register/signal access ---

    const cpu::CpuDescriptor& cpu_descriptor() const override { return cpu6502_descriptor(); }
    uint64_t register_value(size_t index) const override {
        return cpu6502_register_value(index, *this);
    }
    void set_register_value(size_t index, uint64_t value) override {
        cpu6502_set_register_value(index, *this, value);
    }
    cpu::SignalStateValue signal_state(size_t index) const override {
        return cpu6502_signal_state(index, cpu(), p(), in_nmi_handler(), in_irq_handler());
    }

    // --- Memory-region model (delegates to the coprocessor's memory map) ---

    std::vector<MemoryRegionDescriptor> get_memory_regions() const override {
        return memory_.get_memory_regions();
    }
    uint8_t peek_region(std::string_view name, uint32_t address) const override {
        return memory_.peek_region(name, address);
    }
    uint8_t read_region(std::string_view name, uint32_t address) override {
        return memory_.read_region(name, address);
    }
    void write_region(std::string_view name, uint32_t address, uint8_t value) override {
        memory_.write_region(name, address, value);
    }
    std::string_view machine_type() const override { return memory_.machine_type(); }

    // --- CPU register accessors (debugger convenience) ---

    uint8_t a() const { return cpu_.cpu().a; }
    uint8_t x() const { return cpu_.cpu().x; }
    uint8_t y() const { return cpu_.cpu().y; }
    uint8_t sp() const { return cpu_.cpu().s.b.l; }
    uint16_t pc() const { return cpu_.cpu().opcode_pc.w; }
    uint8_t p() const { return cpu_.cpu().p.value; }

    // Interrupt handler tracking
    bool in_nmi_handler() const { return cpu_.in_nmi_handler(); }
    bool in_irq_handler() const { return false; }

    void set_a(uint8_t value) { cpu_.cpu().a = value; ++sequence_; }
    void set_x(uint8_t value) { cpu_.cpu().x = value; ++sequence_; }
    void set_y(uint8_t value) { cpu_.cpu().y = value; ++sequence_; }
    void set_sp(uint8_t value) { cpu_.cpu().s.b.l = value; ++sequence_; }
    void set_pc(uint16_t value) {
        cpu_.cpu().opcode_pc.w = value;
        cpu_.cpu().pc.w = value + 1;
        cpu_.cpu().dbus = memory_.peek(value);
        ++sequence_;
    }
    void set_p(uint8_t value) { cpu_.cpu().p.value = value; ++sequence_; }

    // --- Memory access ---

    // The debugger interface uses 32-bit addresses; the 6502 map is 16-bit, so
    // truncate here.
    uint8_t read(uint32_t addr) override { return memory_.read(static_cast<uint16_t>(addr)); }
    void write(uint32_t addr, uint8_t value) override {
        memory_.write(static_cast<uint16_t>(addr), value);
        ++sequence_;
    }
    uint8_t peek(uint32_t addr) const override { return memory_.peek(static_cast<uint16_t>(addr)); }

    CoprocessorMemoryMap& memory() { return memory_; }
    const CoprocessorMemoryMap& memory() const { return memory_; }

    // --- Single-cycle step ---

    // One executed coprocessor CPU cycle (the debugger's cycle step). The live
    // breakpoint/watchpoint checks happen inside step(). This does NOT apply the
    // board-timing tick budget or refresh holds -- those are host-time-driven
    // and live in run_until(); a debugger cycle step is a raw CPU cycle.
    void tick() { step(); }

    void step() override;

    // --- Breakpoint management (sorted vector, modified only while stopped) ---

    void set_breakpoint_entries(std::vector<BreakpointEntry> entries) override {
        std::sort(entries.begin(), entries.end(),
                  [](const BreakpointEntry& a, const BreakpointEntry& b) {
                      return a.start < b.start;
                  });
        with_execution_stopped([&] { breakpoint_entries_ = std::move(entries); });
    }

    const std::vector<BreakpointEntry>& breakpoint_entries() const override { return breakpoint_entries_; }

    void set_breakpoint_hit_callback(BreakpointHitCallback cb) override {
        on_breakpoint_hit_ = std::move(cb);
    }

    // --- Watchpoint management (sorted by start, modified only while stopped) ---

    using WatchpointHitCallback = std::function<void(const WatchpointEntry& wp, uint32_t addr, uint8_t value, bool is_write)>;

    void set_watchpoint_entries(std::vector<WatchpointEntry> entries) override {
        std::sort(entries.begin(), entries.end(),
                  [](const WatchpointEntry& a, const WatchpointEntry& b) {
                      return a.start < b.start;
                  });
        with_execution_stopped([&] { watchpoint_entries_ = std::move(entries); });
    }

    void set_watchpoint_hit_callback(WatchpointHitCallback cb) override {
        on_watchpoint_hit_ = std::move(cb);
    }

    const std::vector<WatchpointEntry>& watchpoint_entries() const override { return watchpoint_entries_; }

    // Direct watchpoint entry management (for C++ tests)
    void add_watchpoint_entry(WatchpointEntry entry) {
        with_execution_stopped([&] {
            watchpoint_entries_.push_back(std::move(entry));
            std::sort(watchpoint_entries_.begin(), watchpoint_entries_.end(),
                      [](const WatchpointEntry& a, const WatchpointEntry& b) {
                          return a.start < b.start;
                      });
        });
    }

    void clear_watchpoint_entries() {
        with_execution_stopped([&] { watchpoint_entries_.clear(); });
    }

    // --- Component access ---

    M6502& cpu() { return cpu_.cpu(); }
    const M6502& cpu() const { return cpu_.cpu(); }

    CoprocessorCpu& coprocessor_cpu() { return cpu_; }
    const CoprocessorCpu& coprocessor_cpu() const { return cpu_; }

    CoprocessorMemoryMap& memory_map() { return memory_; }
    const CoprocessorMemoryMap& memory_map() const { return memory_; }

    TubeCoprocessorBackend& tube_port() { return tube_port_; }
    const TubeCoprocessorBackend& tube_port() const { return tube_port_; }

private:
    // Breakpoint check, performed before a tick at an instruction boundary.
    // Returns true if a breakpoint paused execution (so the caller must stop
    // before executing the instruction at the breakpoint PC). Shared by step()
    // (the live tick path) and run() (tests) so they stay in lock-step.
    bool check_breakpoints();

    // Watchpoint check, performed after a tick (every bus access). Returns true
    // if a watchpoint fired.
    bool check_watchpoints();

    TubeCoprocessorBackend& tube_port_;                     // reference to active port
    CoprocessorMemoryMap memory_;
    CoprocessorCpu cpu_;

    // Board timing (crystal-tick cycle costs, refresh) and the host-time to
    // crystal-tick conversion for the board's ticks-per-host-cycle ratio.
    BoardTiming timing_;
    CoprocessorClock clock_;

    // Board-timing execution state, all in crystal ticks (issue #70). run_until()
    // adds due ticks to the budget and spends read/write ticks per executed cycle;
    // a DRAM refresh holds the CPU for one cycle at the next opcode fetch once the
    // timer reaches the period. Reset by reset(). cycle_count() stays CPU cycles.
    int64_t tick_budget_ = 0;      // unspent host ticks; may briefly go negative
    uint64_t refresh_timer_ = 0;   // ticks since the last refresh (0 = no refresh board)
    bool refresh_pending_ = false; // a refresh is waiting for the next SYNC
    uint64_t refresh_hold_count_ = 0;  // refresh holds performed (diagnostic)

    // ROM image (kept for reset). The full 4 KB 2732 device contents.
    std::array<uint8_t, 4096> rom_;

    // Debugger pause state (plain bool, single-threaded)
    bool paused_ = false;

    // Sequence counter
    uint64_t sequence_ = 0;

    // Halts the host thread that drives this coprocessor, so its debug entry
    // vectors can be mutated safely. Injected by the server (or a test); unset
    // means no other thread drives run_until, so mutation is already safe.
    std::function<void(const std::function<void()>&)> execution_quiescer_;

    // Breakpoint addresses (sorted, checked inline at instruction boundaries)
    std::vector<BreakpointEntry> breakpoint_entries_;
    BreakpointHitCallback on_breakpoint_hit_;

    // Watchpoint entries (sorted by start, checked inline on every bus access)
    std::vector<WatchpointEntry> watchpoint_entries_;
    WatchpointHitCallback on_watchpoint_hit_;
};

}  // namespace beebium

// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_MACHINE_HPP
#define BEEBIUM_MACHINE_HPP

#include "BusStretching.hpp"
#include "Clock.hpp"
#include "ClockBinding.hpp"
#include "CpuBinding.hpp"
#include "ProgramCounterHistogram.hpp"
#include "Types.hpp"
#include "Via6522.hpp"
#include "VideoBinding.hpp"
#include "econet/EconetConcepts.hpp"
#include "serial/SerialConcepts.hpp"

#include <6502/6502.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace beebium {

// IRQ device mask for M6502_SetDeviceIRQ
// The 6502 library supports multiple IRQ sources; each bit represents one source.
// Bit 0: System VIA IRQ, Bit 1: User VIA IRQ, Bit 2: Tube HIRQ
constexpr uint8_t kIrqDeviceMask = 0x07;

// NMI device mask for M6502_SetDeviceNMI
// The 6502 library supports multiple NMI sources; we use separate bits for each source.
// Bit 0: Disc controller NMI (WD1770 INTRQ on Model B+, 8271 on Model B)
// Bit 1: Econet ADLC NMI (MC6854 IRQ gated through INTON/INTOFF flip-flop)
constexpr uint8_t kDiscNmiDeviceMask = 0x01;
constexpr uint8_t kEconetNmiDeviceMask = 0x02;

// Breakpoint hit callback: called on the rare path when a breakpoint address matches.
// Receives the entry (for condition evaluation) and the PC.
using BreakpointHitCallback = std::function<void(const BreakpointEntry& bp, uint32_t pc)>;

// Watchpoint hit callback: same type as CpuBinding's inline check callback
using WatchpointHitCallback = CpuWatchpointHitCallback;

// Machine state that can be serialized/deserialized.
// Parameterized by MemoryPolicy to include memory state.
template<typename MemoryPolicy>
struct MachineState {
    M6502 cpu{};
    MemoryPolicy memory;
    uint64_t cycle_count = 0;
};

// Core BBC Micro emulator, parameterized by CPU and Memory policies.
//
// CpuPolicy must provide:
//   - static constexpr const M6502Config* config
//
// MemoryPolicy must provide:
//   - uint8_t read(uint16_t addr) const
//   - void write(uint16_t addr, uint8_t value)
//   - void reset()
//   - system_via, user_via members (Via6522)
//   - irq_aggregator() method returning aggregator with poll()
//
template<typename CpuPolicy, typename MemoryPolicy>
class Machine {
public:
    // Policy type aliases for external access
    using Cpu = CpuPolicy;
    using Memory = MemoryPolicy;

    using State = MachineState<MemoryPolicy>;
    using CpuBindingType = CpuBinding<MemoryPolicy>;
    using VideoBindingType = VideoBinding<MemoryPolicy>;

    // System clock type: CPU, VIAs, and video all subscribe
    using SystemClockType = Clock<
        ClockBinding<CpuBindingType>,
        ClockBinding<Via6522>,
        ClockBinding<Via6522>,
        ClockBinding<VideoBindingType>
    >;

    Machine()
        : state_()
        , cpu_binding_(state_.cpu, state_.memory)
        , video_binding_(state_.memory)
        , system_clock_(make_system_clock())
    {
        setup_callbacks();
        reset();
    }

    // Note: VideoBinding is now explicitly constructed, taking Hardware by reference.
    // It internally owns a VideoRenderer for pixel generation.

    ~Machine() {
        M6502_Destroy(&state_.cpu);
    }

    // Non-copyable (contains M6502 with pointers)
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    // =========================================================================
    // Reset methods
    // =========================================================================

    // Power-on reset: clear RAM and reset all devices including System VIA.
    // This is the full initialization state, equivalent to powering off and on.
    // MOS will detect the cleared System VIA and perform full initialization.
    void reset() {
        M6502_Init(&state_.cpu, CpuPolicy::config);
        M6502_Reset(&state_.cpu);
        state_.memory.reset();
        video_binding_.reset();
        state_.cycle_count = 0;
        in_reset_ = false;
        in_nmi_handler_ = false;
        in_irq_handler_ = false;
        ++sequence_;
    }

    // Soft reset (Break key): reset CPU and peripherals, but preserve System VIA.
    // The System VIA's preserved state allows MOS to detect this as a warm reset.
    // Does NOT clear RAM - programs and variables survive.
    // This is what happens when the Break key is released.
    void soft_reset() {
        M6502_Init(&state_.cpu, CpuPolicy::config);
        M6502_Reset(&state_.cpu);
        state_.memory.soft_reset();
        video_binding_.reset();
        // Do NOT reset cycle_count - maintains timing continuity
        in_reset_ = false;
        in_nmi_handler_ = false;
        in_irq_handler_ = false;
        ++sequence_;
    }

    // =========================================================================
    // Break key handling (directly connected to reset circuit)
    // =========================================================================

    // Assert Break key (hold reset line low, halting CPU)
    // While Break is held, the CPU is frozen - step() will not execute instructions.
    //
    // Called from the KeyboardService gRPC thread; the emulation loop may be
    // mid-cycle calling cpu.tfn. Pause and drain the loop before mutating CPU
    // state so we don't race against `(*cpu.tfn)(&cpu)`. Restore the original
    // paused state on exit so a debugger pause is preserved.
    void break_down() {
        with_emulation_paused([this] {
            in_reset_ = true;
            M6502_Halt(&state_.cpu);
            ++sequence_;
        });
    }

    // Release Break key (begin reset sequence)
    // This always performs a soft reset - hardware does NOT distinguish Ctrl-Break.
    // The System VIA is preserved, and MOS checks the keyboard matrix during its
    // reset sequence. If Ctrl is held, MOS itself clears the VIA configuration
    // to force a "hard reset" behavior.
    //
    // The soft_reset() does memset(&cpu, 0, sizeof) inside M6502_Init -- it MUST
    // run while the emulation loop is idle, otherwise a torn read of cpu.tfn or
    // a call through a NULL tfn segfaults the server (issues #27 / #39).
    void break_up() {
        with_emulation_paused([this] {
            soft_reset();
        });
    }

    // Check if Break key is currently held (CPU halted)
    bool is_in_reset() const {
        return in_reset_;
    }

    // Execute one CPU cycle
    void step() {
        // Advance host time in the Tube socket, as the first action on every
        // path. cycle_count advances by exactly one per step(). The socket runs
        // the coprocessor in batches of up to MAX_COPROCESSOR_SKEW host cycles
        // (Step 3), and exactly before any host register access; between those
        // points the coprocessor lags by fewer than that bound, which the skew
        // contract permits.
        state_.memory.tube_socket.host_cycle(state_.cycle_count);

        // Handle 1MHz bus stretch cycles.
        // During stretch, CPU is halted. VIAs have already been pre-ticked
        // by CpuBinding before the memory access, so we only tick video here.
        if (stretch_cycles_remaining_ > 0) {
            tick_stretch_cycle();
            --stretch_cycles_remaining_;
            ++state_.cycle_count;
            ++sequence_;
            return;
        }

        // The coprocessor has already been run forward to this host cycle at
        // the top of step(), so its register writes are visible before the
        // host CPU ticks (B2 ordering).

        // Pass current cycle to CpuBinding for 1MHz synchronization calculations
        cpu_binding_.set_current_cycle(state_.cycle_count);

        // Tick host CPU - this may pre-tick VIAs for 1MHz synchronization
        const bool is_rising = (state_.cycle_count & 1) != 0;
        if (is_rising) {
            cpu_binding_.tick_rising();
        } else {
            cpu_binding_.tick_falling();
        }

        // Tick order matters for same-cycle vsync detection:
        // - On rising edge: VIA ticks (no video)
        // - On falling edge: Video ticks FIRST (updates vsync), then VIA (detects edge)
        // This matches jsbeeb where setVBlankInt() is called from video.polltime()
        // and immediately triggers VIA CA1 edge detection on the same cycle.
        if (is_rising) {
            // Rising edge: VIA + video (in 2MHz character clock modes)
            // In 2MHz mode the CRTC must tick every 2MHz cycle, not just
            // on falling edges. Without this, bitmap modes produce 25 Hz
            // VSYNC instead of 50 Hz.
            if (video_binding_.clock_rate() == ClockRate::Rate_2MHz) {
                video_binding_.tick_falling();
            }
            if (!cpu_binding_.system_via_pre_ticked()) {
                state_.memory.system_via.tick_rising();
            }
            if (!cpu_binding_.user_via_pre_ticked()) {
                state_.memory.user_via.tick_rising();
            }
        } else {
            // Falling edge: Video first (updates vsync), then VIA (detects edge)
            video_binding_.tick_falling();
            // Skip only the VIA that was pre-ticked by CpuBinding
            if (!cpu_binding_.system_via_pre_ticked()) {
                state_.memory.system_via.tick_falling();
            }
            if (!cpu_binding_.user_via_pre_ticked()) {
                state_.memory.user_via.tick_falling();
            }
        }

        // Tick sound chip at 2 MHz if audio output is enabled
        if (state_.memory.audio_buffer) {
            state_.memory.sound_chip.tick(state_.memory.audio_buffer.value());
        }

        // Tick the type-ahead queue to process queued keystrokes
        state_.memory.system_via_peripheral.tick_type_ahead();

        // Tick Econet ADLC at 2MHz (no-op when socket is empty).
        // The ADLC sits on the 2MHz bus at &FEA0-&FEBF and needs clocking
        // on every E-clock edge to drive the byte trickle timer.
        if constexpr (HasEconetSocket<MemoryPolicy>) {
            if (is_rising) {
                state_.memory.econet_socket.tick_rising();
            } else {
                state_.memory.econet_socket.tick_falling();
            }
        }

        // Tick the serial ACIA + Serial ULA at 2MHz. The Serial ULA owns the
        // transmit/receive bit clocks; exactly one of tick_rising/tick_falling
        // fires per 2MHz cycle, so the bit clock advances once per CPU cycle.
        // The ACIA IRQ is sampled below via poll_irq() (shared CPU IRQ line).
        if constexpr (HasSerialSocket<MemoryPolicy>) {
            if (is_rising) {
                state_.memory.serial_socket.tick_rising();
            } else {
                state_.memory.serial_socket.tick_falling();
            }
        }

        // Check if the CPU's memory access triggered bus stretching
        if (cpu_binding_.needs_stretch()) {
            // The accessed VIA (if any) was pre-ticked by CpuBinding for
            // synchronization. During stretch cycles, we must continue ticking
            // the non-accessed VIA(s) so their timers don't lose cycles.
            stretch_cycles_remaining_ = cpu_binding_.stretch_cycle_count();
            stretch_via_pre_tick_mask_ = cpu_binding_.via_pre_tick_mask();
            cpu_binding_.clear_stretch();
        }

        // IRQ handling - poll aggregator and set CPU IRQ line
        uint8_t irq_mask = state_.memory.poll_irq();
        M6502_SetDeviceIRQ(&state_.cpu, kIrqDeviceMask, irq_mask ? 1 : 0);

        // NMI handling — disc controller at 1MHz, Econet at 2MHz.
        //
        // Disc NMI: only update on 1MHz clock edges (every other 2MHz cycle).
        // The WD1770 disc controller runs at 1MHz. Updating NMI every 2MHz cycle
        // causes DRQ to toggle too rapidly: after the NMI handler reads the data
        // register (clearing DRQ), the next tick() would immediately set DRQ for
        // the next byte, creating a new falling edge on /NMI before the handler
        // completes RTI. This causes NMIs to stack up infinitely.
        if ((state_.cycle_count & 1) == 0) {
            uint8_t nmi_mask = state_.memory.poll_nmi();
            M6502_SetDeviceNMI(&state_.cpu, kDiscNmiDeviceMask, nmi_mask ? 1 : 0);
        }

        // Econet NMI: update every 2MHz cycle (ADLC is a 2MHz device).
        // The ADLC IRQ output is gated through the INTON/INTOFF flip-flop
        // in EconetSocket::nmi_pending(). When all three conditions are met
        // (socket enabled, NMI flip-flop set, ADLC IRQ active), NMI is asserted.
        if constexpr (HasEconetSocket<MemoryPolicy>) {
            uint8_t econet_nmi = state_.memory.econet_socket.nmi_pending() ? 1 : 0;
            M6502_SetDeviceNMI(&state_.cpu, kEconetNmiDeviceMask, econet_nmi);
        }

        // Interrupt handler tracking: detect entry via M6502ReadType_Interrupt
        // and exit via RTI (opcode $40).  NMI has priority: if nmi_flags is
        // set at interrupt entry, it's an NMI; otherwise it's IRQ/BRK.
        if (state_.cpu.read == M6502ReadType_Interrupt) {
            if (state_.cpu.nmi_flags != 0) {
                in_nmi_handler_ = true;
            } else {
                in_irq_handler_ = true;
            }
        }
        if (M6502_IsAboutToExecute(&state_.cpu) && state_.cpu.dbus == 0x40) {
            // RTI: NMI exit takes priority (NMI can nest inside IRQ).
            if (in_nmi_handler_) {
                in_nmi_handler_ = false;
            } else if (in_irq_handler_) {
                in_irq_handler_ = false;
            }
        }

        ++state_.cycle_count;
        ++sequence_;
    }

    // Execute for the given number of cycles, or until paused (e.g., by breakpoint)
    void run(uint64_t cycles) {
        struct RunGuard {
            std::atomic<bool>& flag;
            RunGuard(std::atomic<bool>& f) : flag(f) { flag.store(true, std::memory_order_release); }
            ~RunGuard() { flag.store(false, std::memory_order_release); }
        } guard{in_run_};

        const uint64_t target = state_.cycle_count + cycles;
        while (state_.cycle_count < target && !paused_.load()) {
            // Check breakpoints before step(), when all register updates from
            // the previous instruction have been applied by the T0 tfn that set
            // read=Opcode.  Use opcode_pc (the address of the opcode about to
            // be decoded), not pc (which has already been advanced past it by
            // M6502_NextInstruction's post-increment).
            if (!breakpoint_entries_.empty() && M6502_IsAboutToExecute(&state_.cpu)) {
                uint16_t pc = state_.cpu.opcode_pc.w;
                bool hit_stop = false;
                for (auto& bp : breakpoint_entries_) {
                    if (bp.start > pc) break;  // sorted by start: early exit
                    if (bp.matches(pc)) {
                        if (on_breakpoint_hit_) on_breakpoint_hit_(bp, pc);
                        if (paused_.load()) { hit_stop = true; break; }
                    }
                }
                if (hit_stop) break;  // fall through to the sync below
            }

            step();
        }

        // The host has stopped (chunk finished, paused, or a breakpoint/
        // watchpoint hit). Sync the coprocessor to the host so a stopped
        // machine presents both processors at the same time to the debugger
        // and to GetTubeState.
        state_.memory.tube_socket.run_coprocessor_until(state_.cycle_count);
    }

    // Execute one complete instruction (variable cycles)
    // Returns the number of cycles taken
    uint64_t step_instruction() {
        in_run_.store(true, std::memory_order_release);
        const uint64_t start = state_.cycle_count;
        do {
            step();
        } while (!M6502_IsAboutToExecute(&state_.cpu));
        in_run_.store(false, std::memory_order_release);
        return state_.cycle_count - start;
    }

    // Called by the debugger after a single-step batch (StepInstruction /
    // StepCycle), the symmetric partner of prepare_for_step(). Syncs the
    // coprocessor to the host so a single-stepped machine shows both processors
    // within a cycle of each other. (CpuDebugTarget provides a no-op
    // finish_step(); only the host has a coprocessor to sync.)
    void finish_step() {
        state_.memory.tube_socket.run_coprocessor_until(state_.cycle_count);
    }

    // State access
    const State& state() const { return state_; }
    State& state() { return state_; }

    // CPU access
    const M6502& cpu() const { return state_.cpu; }
    M6502& cpu() { return state_.cpu; }

    // Memory access
    const MemoryPolicy& memory() const { return state_.memory; }
    MemoryPolicy& memory() { return state_.memory; }

    // Cycle counter
    uint64_t cycle_count() const { return state_.cycle_count; }

    // Sequence counter (increments on any mutation, for change detection)
    uint64_t sequence() const { return sequence_.load(); }

    // Debug pause/resume for debugger integration.
    //
    // Pause state has two independent sources, kept apart so neither can lose
    // the other's intent (see paused_ and the members below):
    //  - user_paused_: the logical pause a debugger client sets with pause()
    //    and clears with resume(). is_paused() reports this.
    //  - quiesce_depth_: raised by with_emulation_paused while a caller mutates
    //    machine state the emulation thread would otherwise touch.
    // The emulation loop reads the derived flag paused_ == (user_paused_ ||
    // quiesce_depth_ > 0), recomputed under debug_mutex_ whenever either source
    // changes. Reporting only the logical state means a transient quiesce does
    // not make the machine look stopped to a client, and a resume() cannot
    // release a machine another caller is still quiescing.
    bool is_paused() const { return user_paused_.load(std::memory_order_acquire); }

    void pause() {
        std::lock_guard<std::mutex> lock(debug_mutex_);
        user_paused_.store(true, std::memory_order_release);
        recompute_paused_locked();
        // Present both processors at the same cycle when we can prove the
        // emulation thread is idle and no quiescer owns the machine. The lock is
        // held across the sync so a quiescing caller cannot begin touching
        // machine state while we drive the coprocessor. When the emulation loop
        // owns execution (in_run_/emulation_busy_) or a quiesce is in progress,
        // that owner is responsible for the coprocessor instead.
        if (!in_run_.load(std::memory_order_acquire) &&
            !emulation_busy_.load(std::memory_order_acquire) &&
            quiesce_depth_ == 0) {
            state_.memory.tube_socket.run_coprocessor_until(state_.cycle_count);
        }
        ++sequence_;
    }

    void resume() {
        {
            std::lock_guard<std::mutex> lock(debug_mutex_);
            user_paused_.store(false, std::memory_order_release);
            recompute_paused_locked();
        }
        // Wake wait_if_paused so it re-tests paused_: it stays parked if a
        // quiesce is still in progress, and runs on otherwise.
        debug_cv_.notify_all();
        ++sequence_;
    }

    // Call before stepping cycles (no-op now, retained for interface compatibility).
    void prepare_for_step() {}

    // Wait until the emulation thread is idle: neither inside run() (in_run_)
    // nor doing per-iteration work outside run() under an EmulationBusyScope
    // (emulation_busy_). Call from a quiescing thread after raising the pause so
    // the body that follows runs single-threaded against machine state.
    void wait_until_idle() {
        while (in_run_.load(std::memory_order_acquire) ||
               emulation_busy_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    // Run `f` with the emulation thread guaranteed not to be touching machine
    // state. The machine is quiesced for the duration of `f` and its prior
    // logical pause state is left untouched afterwards, so a debugger that had
    // stopped the machine stays stopped and one that was running runs on.
    //
    // Use this from any thread (a gRPC service, an extension dispatcher) that
    // mutates CPU/memory/device state the emulation loop may read or write.
    // Never call it from the emulation thread itself: it waits for that thread
    // to go idle and would wait for itself forever.
    template<typename F>
    void with_emulation_paused(F&& f) {
        // Serialise distinct quiescing callers, so only one body runs against
        // machine state at a time. The mutex is RECURSIVE by necessity, not
        // convenience: a quiescing body may legitimately call a Machine mutator
        // that itself quiesces on the same thread -- e.g. DebuggerService's
        // with_execution_change wraps a handler that calls
        // Machine::set_watchpoint_entries, which is itself a with_emulation_paused.
        // A non-recursive mutex would self-deadlock on that nesting. Do not
        // "fix" it to std::mutex.
        std::lock_guard<std::recursive_mutex> quiesce_lock(quiesce_mutex_);
        begin_quiesce();
        wait_until_idle();
        // Restore the derived pause flag however `f` returns (including by
        // throwing), so an exception in a mutator cannot strand the machine
        // paused.
        struct EndQuiesceGuard {
            Machine* machine;
            ~EndQuiesceGuard() { machine->end_quiesce(); }
        } end_guard{this};
        f();
    }

    // Block until not paused - call from emulation loop.
    // Returns immediately if shutdown is requested, allowing clean exit.
    /// Block while the debugger has paused execution. Returns true if it
    /// actually blocked (the machine was paused), so the caller can re-anchor
    /// timing that must not count the paused interval (see the pacing clock).
    // `on_wake` runs on each pass of the wait, so housekeeping the emulation
    // loop would otherwise perform every iteration keeps running while the
    // machine is paused. A safe eject, for instance, must still complete on a
    // paused machine: the drive is standing still, which is precisely when it
    // is safe to let the disc go.
    bool wait_if_paused(const std::function<void()>& on_wake = {}) {
        std::unique_lock<std::mutex> lock(debug_mutex_);
        bool blocked = false;
        while (paused_.load() && !shutdown_requested_.load()) {
            blocked = true;
            // Run housekeeping only when the pause is purely logical. While a
            // quiescer holds the machine (quiesce_depth_ > 0) its body may be
            // mutating exactly the device state on_wake would touch (e.g. a disc
            // controller being ejected vs. a drive tick), so on_wake must stand
            // down. The busy flag is raised under debug_mutex_ so a quiescer
            // that acquires the lock next sees us busy and wait_until_idle waits
            // for on_wake to finish before its body runs.
            if (on_wake && quiesce_depth_ == 0) {
                emulation_busy_.store(true, std::memory_order_release);
                lock.unlock();
                on_wake();
                emulation_busy_.store(false, std::memory_order_release);
                lock.lock();
                if (!paused_.load() || shutdown_requested_.load()) {
                    break;
                }
            }
            debug_cv_.wait_for(lock, std::chrono::milliseconds(100));
        }
        return blocked;
    }

    // Request clean shutdown - unblocks wait_if_paused()
    void request_shutdown() {
        shutdown_requested_.store(true);
        debug_cv_.notify_all();  // Wake up any blocked wait_if_paused()
    }

    // Check if shutdown has been requested
    bool shutdown_requested() const { return shutdown_requested_.load(); }

    // CPU register accessors (debugger convenience)
    uint8_t a() const { return state_.cpu.a; }
    uint8_t x() const { return state_.cpu.x; }
    uint8_t y() const { return state_.cpu.y; }
    uint8_t sp() const { return state_.cpu.s.b.l; }
    uint16_t pc() const { return state_.cpu.opcode_pc.w; }
    uint8_t p() const { return state_.cpu.p.value; }

    // Interrupt handler tracking
    bool in_nmi_handler() const { return in_nmi_handler_; }
    bool in_irq_handler() const { return in_irq_handler_; }

    // CPU register setters (for debugger) - each increments sequence_
    void set_a(uint8_t value) { state_.cpu.a = value; ++sequence_; }
    void set_x(uint8_t value) { state_.cpu.x = value; ++sequence_; }
    void set_y(uint8_t value) { state_.cpu.y = value; ++sequence_; }
    void set_sp(uint8_t value) { state_.cpu.s.b.l = value; ++sequence_; }
    void set_pc(uint16_t value) {
        state_.cpu.opcode_pc.w = value;
        state_.cpu.pc.w = value + 1;
        state_.cpu.dbus = state_.memory.peek(value);
        ++sequence_;
    }
    void set_p(uint8_t value) { state_.cpu.p.value = value; ++sequence_; }

    // Direct memory access (convenience)
    // Note: read() is non-const because some devices have read side effects (e.g., VIA interrupt flags)
    uint8_t read(uint16_t addr) { return state_.memory.read(addr); }
    void write(uint16_t addr, uint8_t value) { state_.memory.write(addr, value); ++sequence_; }

    // Side-effect-free read for debugger inspection
    uint8_t peek(uint16_t addr) const { return state_.memory.peek(addr); }

    // Breakpoint entry management. The emulation loop iterates this vector every
    // instruction boundary, so the swap must happen with that thread idle:
    // with_emulation_paused parks it across the swap. It must therefore NOT be
    // called from the emulation thread itself (it would wait for itself forever);
    // the breakpoint/watchpoint hit callbacks, which run on that thread, only
    // read entries and increment hit_count -- they never call these mutators.
    void set_breakpoint_entries(std::vector<BreakpointEntry> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const BreakpointEntry& a, const BreakpointEntry& b) {
                      return a.start < b.start;
                  });
        with_emulation_paused([&] { breakpoint_entries_ = std::move(entries); });
    }

    void set_breakpoint_hit_callback(BreakpointHitCallback cb) {
        on_breakpoint_hit_ = std::move(cb);
    }

    const std::vector<BreakpointEntry>& breakpoint_entries() const { return breakpoint_entries_; }

    // Debugger watchpoint entry management. As with breakpoints, the swap runs
    // with the emulation loop paused (it iterates the vector on every bus
    // access); never call from the emulation thread.
    void set_watchpoint_entries(std::vector<WatchpointEntry> entries) {
        std::sort(entries.begin(), entries.end(),
                  [](const WatchpointEntry& a, const WatchpointEntry& b) {
                      return a.start < b.start;
                  });
        with_emulation_paused([&] {
            watchpoint_entries_ = std::move(entries);
            cpu_binding_.set_watchpoint_entries(&watchpoint_entries_);
        });
    }

    void set_watchpoint_hit_callback(WatchpointHitCallback cb) {
        on_watchpoint_hit_ = std::move(cb);
        cpu_binding_.set_watchpoint_hit_callback(&on_watchpoint_hit_);
    }

    const std::vector<WatchpointEntry>& watchpoint_entries() const { return watchpoint_entries_; }

    // Direct watchpoint entry management (for C++ tests and in-process use).
    // Adds an entry and re-sorts the vector.
    void add_watchpoint_entry(WatchpointEntry entry) {
        with_emulation_paused([&] {
            watchpoint_entries_.push_back(std::move(entry));
            std::sort(watchpoint_entries_.begin(), watchpoint_entries_.end(),
                      [](const WatchpointEntry& a, const WatchpointEntry& b) {
                          return a.start < b.start;
                      });
            cpu_binding_.set_watchpoint_entries(&watchpoint_entries_);
        });
    }

    void clear_watchpoint_entries() {
        with_emulation_paused([&] {
            watchpoint_entries_.clear();
            cpu_binding_.set_watchpoint_entries(&watchpoint_entries_);
        });
    }

    // PC histogram for instruction execution profiling
    void set_pc_histogram(ProgramCounterHistogram* histogram) { pc_histogram_ = histogram; }
    ProgramCounterHistogram* pc_histogram() const { return pc_histogram_; }

    // Access to bindings for testing/debugging
    CpuBindingType& cpu_binding() { return cpu_binding_; }
    VideoBindingType& video_binding() { return video_binding_; }

    // RAII scope the emulation thread holds around the per-iteration work it
    // performs outside run() -- ticking disc drives, adjusting emulation speed --
    // and the run() call itself. It generalises run()'s in_run_ guard so
    // wait_until_idle(), and therefore every quiescing caller, treats the
    // emulation thread as busy for everything it touches, not only run().
    //
    // Construction re-tests paused_ under debug_mutex_: if a quiescer raised the
    // pause in the window since the loop last checked, the scope is inactive and
    // the caller must skip the work and loop back to wait_if_paused rather than
    // touch machine state. Testing the flag and setting the busy flag under the
    // same lock closes the window between the loop's pause check and the work.
    class EmulationBusyScope {
    public:
        explicit EmulationBusyScope(Machine& machine) : machine_(&machine) {
            std::lock_guard<std::mutex> lock(machine_->debug_mutex_);
            if (machine_->paused_.load(std::memory_order_acquire)) {
                return;  // a quiescer owns the machine; stay out of its way
            }
            machine_->emulation_busy_.store(true, std::memory_order_release);
            active_ = true;
        }
        ~EmulationBusyScope() {
            if (active_) {
                machine_->emulation_busy_.store(false, std::memory_order_release);
            }
        }
        EmulationBusyScope(const EmulationBusyScope&) = delete;
        EmulationBusyScope& operator=(const EmulationBusyScope&) = delete;

        // True when the scope holds the busy guard; false when a pause was in
        // effect at construction and the caller must not touch machine state.
        bool active() const { return active_; }

    private:
        Machine* machine_;
        bool active_ = false;
    };

private:
    // Recompute the derived pause flag the emulation loop reads. Must hold
    // debug_mutex_ so the two sources are sampled atomically together.
    void recompute_paused_locked() {
        paused_.store(user_paused_.load(std::memory_order_relaxed) || quiesce_depth_ > 0,
                      std::memory_order_release);
    }

    // Raise/lower the quiesce count around a with_emulation_paused body.
    void begin_quiesce() {
        std::lock_guard<std::mutex> lock(debug_mutex_);
        ++quiesce_depth_;
        recompute_paused_locked();
    }
    void end_quiesce() {
        bool now_running;
        {
            std::lock_guard<std::mutex> lock(debug_mutex_);
            --quiesce_depth_;
            recompute_paused_locked();
            now_running = !paused_.load(std::memory_order_relaxed);
        }
        // Only the last unwinding quiesce that leaves the machine logically
        // running needs to wake a parked wait_if_paused.
        if (now_running) {
            debug_cv_.notify_all();
        }
    }

    State state_;
    CpuBindingType cpu_binding_;
    VideoBindingType video_binding_;
    SystemClockType system_clock_;

    std::vector<BreakpointEntry> breakpoint_entries_;    // sorted by address, modified only while stopped
    BreakpointHitCallback on_breakpoint_hit_;           // rare-path callback
    std::vector<WatchpointEntry> watchpoint_entries_;   // sorted by start, modified only while stopped
    WatchpointHitCallback on_watchpoint_hit_;           // rare-path callback
    ProgramCounterHistogram* pc_histogram_ = nullptr;

    // Debug pause/resume state (for debugger attach). debug_mutex_ guards the
    // relationship between the two pause sources and the derived flag; the
    // atomics are read lock-free by the emulation loop on its hot path.
    mutable std::mutex debug_mutex_;
    std::condition_variable debug_cv_;
    // Derived flag the emulation loop reads: user_paused_ || quiesce_depth_ > 0.
    // Written only via recompute_paused_locked() under debug_mutex_.
    std::atomic<bool> paused_{false};
    std::atomic<bool> user_paused_{false};  // Logical pause: pause()/resume()
    int quiesce_depth_ = 0;                  // with_emulation_paused nesting (debug_mutex_)
    // Serialises distinct with_emulation_paused callers; recursive so a
    // quiescing body may re-enter on the same thread (see with_emulation_paused).
    std::recursive_mutex quiesce_mutex_;
    std::atomic<bool> shutdown_requested_{false};  // For clean server shutdown
    std::atomic<bool> in_run_{false};              // True while run() is executing
    // True while the emulation thread does per-iteration work outside run()
    // under an EmulationBusyScope; wait_until_idle() waits on it too.
    std::atomic<bool> emulation_busy_{false};
    std::atomic<uint64_t> sequence_{0};  // Increments on any mutation

    // Break key state (true when Break is held, CPU halted)
    bool in_reset_ = false;

    // Interrupt handler tracking (for debugger)
    bool in_nmi_handler_ = false;
    bool in_irq_handler_ = false;

    // 1MHz bus stretch handling
    // When CPU accesses a 1MHz peripheral, we insert extra cycles
    // where peripherals tick but the CPU doesn't.
    // stretch_via_pre_tick_mask_ records which VIA(s) were pre-ticked
    // by CpuBinding so we can continue ticking the others during stretch.
    uint8_t stretch_cycles_remaining_ = 0;
    uint8_t stretch_via_pre_tick_mask_ = 0;

    SystemClockType make_system_clock() {
        return make_clock(
            make_clock_binding(cpu_binding_),
            make_clock_binding(state_.memory.system_via),
            make_clock_binding(state_.memory.user_via),
            make_clock_binding(video_binding_)
        );
    }

    // Tick peripherals during stretch cycles.
    // The accessed VIA (if any) was pre-ticked by CpuBinding for 1MHz
    // synchronization. Non-accessed VIAs must continue ticking so their
    // timers don't lose cycles. CPU is halted waiting for bus alignment.
    void tick_stretch_cycle() {
        const uint64_t cycle = state_.cycle_count;
        const bool is_rising = (cycle & 1) != 0;

        if (is_rising) {
            // Video ticks on rising edges too in 2MHz character clock modes
            if (video_binding_.clock_rate() == ClockRate::Rate_2MHz) {
                video_binding_.tick_falling();
            }
            // Tick VIAs that were NOT pre-ticked
            if (!(stretch_via_pre_tick_mask_ & CpuBindingType::kPreTickSystemVia)) {
                state_.memory.system_via.tick_rising();
            }
            if (!(stretch_via_pre_tick_mask_ & CpuBindingType::kPreTickUserVia)) {
                state_.memory.user_via.tick_rising();
            }
        } else {
            // Video always ticks on falling edges
            video_binding_.tick_falling();
            // Tick VIAs that were NOT pre-ticked
            if (!(stretch_via_pre_tick_mask_ & CpuBindingType::kPreTickSystemVia)) {
                state_.memory.system_via.tick_falling();
            }
            if (!(stretch_via_pre_tick_mask_ & CpuBindingType::kPreTickUserVia)) {
                state_.memory.user_via.tick_falling();
            }
        }

        // Tick Econet during stretch cycles. Both the FourWayHandshake
        // (network-layer protocol timers) and the ADLC (byte trickle)
        // must advance so that incoming frames can be received even when
        // the host CPU is halted by Tube or 1MHz bus stretch. The ADLC
        // handles FIFO-full conditions by stalling the byte trickle
        // (retrying on the next tick) rather than discarding bytes,
        // which prevents overrun during long stretch periods.
        if constexpr (HasEconetSocket<MemoryPolicy>) {
            if (is_rising) {
                state_.memory.econet_socket.tick_rising();
            } else {
                state_.memory.econet_socket.tick_falling();
            }
        }

        // Keep the serial bit clocks advancing during bus stretch cycles too,
        // so in-flight characters are not stalled while the CPU is halted.
        if constexpr (HasSerialSocket<MemoryPolicy>) {
            if (is_rising) {
                state_.memory.serial_socket.tick_rising();
            } else {
                state_.memory.serial_socket.tick_falling();
            }
        }

        // The disc controller runs on the 1MHz bus and must be ticked during
        // bus stretch cycles. Without this, the pulse-level WD1770 misses
        // ticks whenever the CPU accesses the I/O region, causing the disc
        // head to stall and byte timing to drift.
        if ((cycle & 1) == 0) {
            uint8_t nmi_mask = state_.memory.poll_nmi();
            M6502_SetDeviceNMI(&state_.cpu, kDiscNmiDeviceMask, nmi_mask ? 1 : 0);
        }

        // Econet NMI during stretch cycles
        if constexpr (HasEconetSocket<MemoryPolicy>) {
            uint8_t econet_nmi = state_.memory.econet_socket.nmi_pending() ? 1 : 0;
            M6502_SetDeviceNMI(&state_.cpu, kEconetNmiDeviceMask, econet_nmi);
        }
    }

    void setup_callbacks() {
        // Instruction callback - records PC histogram
        cpu_binding_.set_instruction_callback(
            [this](uint16_t pc) {
                if (pc_histogram_) {
                    pc_histogram_->record(pc);
                }
            }
        );
    }
};

} // namespace beebium

#endif // BEEBIUM_MACHINE_HPP

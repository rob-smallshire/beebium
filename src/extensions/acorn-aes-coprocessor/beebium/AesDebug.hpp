// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.


// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// AesDebug.hpp, the debugger's view of the AES
// version 2.0.0, 20 November 2026.

// AesDebug: Beebium's debugger reads and sets the ARM through this: the registers and signals,
// memory, steps and Reset, and breakpoints and watchpoints.
//
// Beebium's terms, as this file uses them: the CPU descriptor is the
// list of registers, flags and signals the debugger shows, each with its width; a signal is an
// input line such as IRQ; the execution quiescer is Beebium's function that halts the thread
// running the AES while a debugger request runs; a memory region is a named address range
// (here ram, tube and rom) with flags saying what the debugger may do there; a breakpoint or
// watchpoint entry is an address range from the debugger, and the hit callback is how Beebium
// is told of a hit, counts it and pauses the AES.

#pragma once

#include "AesTube.hpp"

#include <beebium/extension/CpuDebugTarget.hpp>

#include <functional>
#include <string_view>
#include <vector>

// The debugger's link to the AES. It implements Beebium's CpuDebugTarget interface for Beebium's
// debugger, TubeStops (AesTube.hpp) to be asked before each instruction, and ArmWatch
// (ArmCore.hpp) to be told of each load and store. AesExtension makes it over the AesTube.
class AesDebug : public beebium::CpuDebugTarget, public TubeStops, public ArmWatch {
public:
    // A link to `tube`, with no breakpoints or watchpoints set.
    explicit AesDebug(AesTube& tube) : tube_(tube) {}

    // Registers in display order: R0-R14 as the mode sees them, R15, PSR, then every bank.
    enum : size_t { kR15 = 15, kPsr = 16, kFirstBank = 17 };

    // The registers and signals: what there is, and reading and setting them.
    const beebium::cpu::CpuDescriptor& cpu_descriptor() const override;
    uint64_t register_value(size_t index) const override;
    void set_register_value(size_t index, uint64_t value) override;
    beebium::cpu::SignalStateValue signal_state(size_t index) const override;

    // In crystal ticks, the board's own clock, because the ARM's cycles have no fixed length here.
    uint64_t cycle_count() const override { return tube_.ticks_spent(); }
    // Instructions run, as the debugger's sequence count.
    uint64_t sequence() const override { return tube_.instructions(); }
    // Pausing and resuming the AES, passed to AesTube.
    bool is_paused() const override { return tube_.is_paused(); }
    void pause() override { tube_.pause(); }
    void resume() override { tube_.resume(); }
    // Runs fn with the AES halted, through the quiescer Beebium sets here.
    void with_execution_stopped(const std::function<void()>& fn) override;
    // Beebium hands over its quiescer here.
    void set_execution_quiescer(std::function<void(const std::function<void()>&)> quiescer) override {
        quiescer_ = std::move(quiescer);
    }

    // Steps run whole instructions, because the core cannot stop part-way through one; so a cycle
    // step is an instruction step, and mid_instruction keeps its default, false (Beebium's
    // CpuDebugTarget.hpp). step_instruction returns the ticks the instruction cost. After a reset
    // Beebium's debugger steps once by itself, so reset does no more.
    void reset() override { tube_.reset(); }
    void step() override { tube_.step_one(); }
    uint64_t step_instruction() override { return tube_.step_one(); }
    // Nothing to do: a step runs on the caller's thread, and the AES has no thread of its own to
    // prepare or wait for.
    void prepare_for_step() override {}
    void wait_until_idle() override {}

    // On resuming from a breakpoint, the instruction stopped on is not checked again, or the ARM
    // would stop at once on the same breakpoint (Beebium's CpuDebugTarget.hpp).
    void step_over_breakpoints_once() override;

    // Memory, as the ARM sees it but with no side effect anywhere, so read is the same as peek.
    // Beebium's flat memory write cannot be refused, so outside RAM it is silently ignored. A
    // region write outside ram throws, which Beebium reports. The regions are ram, tube and rom.
    uint8_t read(uint32_t addr) override { return peek(addr); }
    uint8_t peek(uint32_t addr) const override { return tube_.memory().peek(addr); }
    void write(uint32_t addr, uint8_t value) override { tube_.memory().poke(addr, value); }
    std::vector<beebium::MemoryRegionDescriptor> get_memory_regions() const override;
    uint8_t peek_region(std::string_view name, uint32_t address) const override;
    uint8_t read_region(std::string_view name, uint32_t address) override { return peek_region(name, address); }
    void write_region(std::string_view name, uint32_t address, uint8_t value) override;
    std::string_view machine_type() const override { return "AES"; }

    // Breakpoints before an instruction, watchpoints on each byte a load or store moves,
    // matched here in 32 bits, because Beebium's own matching uses 16-bit addresses. Each list is
    // kept as Beebium sets it, with the callback Beebium is told of a hit through.
    const std::vector<beebium::BreakpointEntry>& breakpoint_entries() const override { return breakpoints_; }
    void set_breakpoint_entries(std::vector<beebium::BreakpointEntry> entries) override;
    void set_breakpoint_hit_callback(BreakpointHitCallback cb) override { breakpoint_hit_ = std::move(cb); }
    const std::vector<beebium::WatchpointEntry>& watchpoint_entries() const override { return watchpoints_; }
    void set_watchpoint_entries(std::vector<beebium::WatchpointEntry> entries) override;
    void set_watchpoint_hit_callback(WatchpointHitCallback cb) override { watchpoint_hit_ = std::move(cb); }

    // TubeStops and ArmWatch: asked before each instruction, told of each load and store.
    void before_instruction(uint32_t pc) override;
    void data_access(uint32_t addr, uint32_t value, int bytes, bool is_write) override;

private:
    // The banked register a register index names.
    uint32_t& bank_register(size_t index) const;
    // Connects the hooks to the AES while anything is set, and disconnects them when nothing is.
    void attach_hooks();
    // A word from memory as peek sees it, lowest address in the low byte.
    uint32_t peek_word(uint32_t at) const;

    AesTube& tube_;
    std::function<void(const std::function<void()>&)> quiescer_;
    std::vector<beebium::BreakpointEntry> breakpoints_;
    std::vector<beebium::WatchpointEntry> watchpoints_;
    BreakpointHitCallback breakpoint_hit_;
    WatchpointHitCallback watchpoint_hit_;
    // Set by step_over_breakpoints_once: the instruction at skip_pc_ is not checked when it next runs.
    bool skip_once_ = false;
    uint32_t skip_pc_ = 0;
};

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

#include "Export.hpp"
#include "CpuDescriptor.hpp"
#include "beebium/MemoryRegion.hpp"
#include "beebium/Types.hpp"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace beebium {

// The whole debugger contract for a CPU, family- and side-agnostic. Both the
// coprocessor's runner and the host's own CPU (through a thin adapter over the
// Machine) implement it, so the one debugger service is a concrete class over a
// CpuDebugTarget& rather than a template. It names no CPU family and carries no
// M6502 reference: the CPU describes its own registers and interrupt signals
// via cpu_descriptor(), and register and signal values are read and written by
// index into that description.
//
// Exported (BEEBIUM_EXT_API) with an out-of-line key function so its typeinfo
// is a single symbol across the plugin boundary, which the server's
// dynamic_cast requires.
class BEEBIUM_EXT_API CpuDebugTarget {
public:
    virtual ~CpuDebugTarget();

    using BreakpointHitCallback =
        std::function<void(const BreakpointEntry& bp, uint16_t pc)>;
    using WatchpointHitCallback =
        std::function<void(const WatchpointEntry& wp, uint16_t addr, uint8_t value, bool is_write)>;

    // --- CPU description and register/signal access by index ---

    // The CPU's registers and interrupt signals, in display order. Stable for
    // the life of the target; the service reads it once and caches it.
    virtual const cpu::CpuDescriptor& cpu_descriptor() const = 0;

    // Register value by index into cpu_descriptor().registers.
    virtual uint64_t register_value(size_t index) const = 0;
    virtual void set_register_value(size_t index, uint64_t value) = 0;

    // Signal state by index into cpu_descriptor().signals.
    virtual cpu::SignalStateValue signal_state(size_t index) const = 0;

    // --- Execution control ---
    virtual uint64_t cycle_count() const = 0;
    virtual uint64_t sequence() const = 0;
    virtual bool is_paused() const = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void reset() = 0;
    virtual void step() = 0;
    virtual uint64_t step_instruction() = 0;
    virtual void prepare_for_step() = 0;
    virtual void wait_until_idle() = 0;
    // Called by the debugger after a single-step batch, the partner of
    // prepare_for_step(). A coprocessor target has nothing to resync and leaves
    // it empty; the host adapter runs the coprocessor to the stopped host time.
    virtual void finish_step() {}

    // --- Flat memory access (CPU address space) ---
    virtual uint8_t read(uint16_t addr) = 0;
    virtual uint8_t peek(uint16_t addr) const = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;

    // PC-aware access, for memory maps whose routing depends on the program
    // counter (the host's shadow-RAM modes). The default ignores the PC; the
    // host adapter overrides it. peek_with_pc is the side-effect-free routing
    // used to inspect banked memory as the CPU would see it at that PC.
    virtual uint8_t read_with_pc(uint16_t addr, uint16_t /*pc*/) { return read(addr); }
    virtual uint8_t peek_with_pc(uint16_t addr, uint16_t /*pc*/) const { return peek(addr); }
    virtual void write_with_pc(uint16_t addr, uint8_t value, uint16_t /*pc*/) { write(addr, value); }

    // --- Memory-region model ---
    virtual std::vector<MemoryRegionDescriptor> get_memory_regions() const = 0;
    virtual uint8_t peek_region(std::string_view name, uint32_t address) const = 0;
    virtual uint8_t read_region(std::string_view name, uint32_t address) = 0;
    virtual void write_region(std::string_view name, uint32_t address, uint8_t value) = 0;
    // The machine's identity, e.g. "model-b-romram" or "Tube65C02". Distinct
    // from cpu_descriptor().family (the CPU family, "6502"): kept separate.
    virtual std::string_view machine_type() const = 0;

    // --- Breakpoints ---
    virtual const std::vector<BreakpointEntry>& breakpoint_entries() const = 0;
    virtual void set_breakpoint_entries(std::vector<BreakpointEntry> entries) = 0;
    virtual void set_breakpoint_hit_callback(BreakpointHitCallback cb) = 0;

    // --- Watchpoints ---
    virtual const std::vector<WatchpointEntry>& watchpoint_entries() const = 0;
    virtual void set_watchpoint_entries(std::vector<WatchpointEntry> entries) = 0;
    virtual void set_watchpoint_hit_callback(WatchpointHitCallback cb) = 0;
};

}  // namespace beebium

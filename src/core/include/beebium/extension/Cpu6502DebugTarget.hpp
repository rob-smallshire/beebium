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

#include "beebium/MemoryRegion.hpp"
#include "beebium/Types.hpp"

#include <6502/6502.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace beebium {

// Abstract memory-region model a 6502 debug target exposes. The parasite's
// memory map implements it; the host machines satisfy the same shape by duck
// typing and need not inherit it. It deliberately has no PC-aware read/write:
// the parasite's map has none, so DebuggerControlServiceImpl's PC-unaware path
// is the existing behaviour and stays so. machine_type() replaces the former
// static MACHINE_TYPE member so the same template instantiates against both a
// concrete host memory map and this interface.
class Cpu6502MemoryModel {
public:
    virtual ~Cpu6502MemoryModel() = default;

    virtual std::vector<MemoryRegionDescriptor> get_memory_regions() const = 0;
    virtual uint8_t peek_region(std::string_view name, uint32_t address) const = 0;
    virtual uint8_t read_region(std::string_view name, uint32_t address) = 0;
    virtual void write_region(std::string_view name, uint32_t address, uint8_t value) = 0;
    virtual std::string_view machine_type() const = 0;
};

// Abstract debugger target: exactly the members service::DebuggerControlServiceImpl<T>
// invokes on its T. The server instantiates the debugger template once against
// this interface and a coprocessor supplies an implementation through
// CoprocessorExtension::debug_target(); ParasiteRunner implements it in the
// plugin, so the server needs no concrete coprocessor type.
class Cpu6502DebugTarget {
public:
    virtual ~Cpu6502DebugTarget() = default;

    using BreakpointHitCallback =
        std::function<void(const BreakpointEntry& bp, uint16_t pc)>;
    using WatchpointHitCallback =
        std::function<void(const WatchpointEntry& wp, uint16_t addr, uint8_t value, bool is_write)>;

    // Execution control.
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

    // Flat memory access.
    virtual uint8_t read(uint16_t addr) = 0;
    virtual uint8_t peek(uint16_t addr) const = 0;
    virtual void write(uint16_t addr, uint8_t value) = 0;

    // Registers.
    virtual uint8_t a() const = 0;
    virtual uint8_t x() const = 0;
    virtual uint8_t y() const = 0;
    virtual uint8_t sp() const = 0;
    virtual uint16_t pc() const = 0;
    virtual uint8_t p() const = 0;
    virtual void set_a(uint8_t value) = 0;
    virtual void set_x(uint8_t value) = 0;
    virtual void set_y(uint8_t value) = 0;
    virtual void set_sp(uint8_t value) = 0;
    virtual void set_pc(uint16_t value) = 0;
    virtual void set_p(uint8_t value) = 0;

    // Interrupt handler state.
    virtual bool in_nmi_handler() const = 0;
    virtual bool in_irq_handler() const = 0;

    // CPU register file (read-only).
    virtual const M6502& cpu() const = 0;

    // Memory-region model.
    virtual Cpu6502MemoryModel& memory() = 0;

    // Breakpoints.
    virtual const std::vector<BreakpointEntry>& breakpoint_entries() const = 0;
    virtual void set_breakpoint_entries(std::vector<BreakpointEntry> entries) = 0;
    virtual void set_breakpoint_hit_callback(BreakpointHitCallback cb) = 0;

    // Watchpoints.
    virtual const std::vector<WatchpointEntry>& watchpoint_entries() const = 0;
    virtual void set_watchpoint_entries(std::vector<WatchpointEntry> entries) = 0;
    virtual void set_watchpoint_hit_callback(WatchpointHitCallback cb) = 0;
};

}  // namespace beebium

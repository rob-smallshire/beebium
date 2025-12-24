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

#pragma once

#include "ClockTypes.hpp"
#include <6502/6502.h>
#include <functional>

namespace beebium {

// Callback types for CpuBinding debugging hooks
using CpuWatchpointCallback = std::function<void(uint16_t addr, uint8_t value, bool is_write)>;
using CpuInstructionCallback = std::function<void(uint16_t pc)>;

// CpuBinding wraps M6502 + MemoryPolicy for clock subscription.
//
// The CPU executes at 2MHz on every cycle (both edges of the clock).
// Memory access is handled internally after each CPU cycle.
//
// Optional debugging callbacks:
// - instruction_callback_: Called at start of each instruction
// - watchpoint_callback_: Called after each memory access
//
template<typename MemoryPolicy>
class CpuBinding {
public:
    M6502& cpu;
    MemoryPolicy& memory;

    // CPU executes every 2MHz cycle - needs both edges
    static constexpr ClockEdge clock_edges = ClockEdge::Both;
    static constexpr ClockRate clock_rate = ClockRate::Rate_2MHz;

    CpuBinding(M6502& cpu, MemoryPolicy& memory)
        : cpu(cpu), memory(memory) {}

    // Execute CPU cycle - called on both rising and falling edges
    void tick_rising() {
        execute_cycle();
    }

    void tick_falling() {
        execute_cycle();
    }

    // Runtime configuration
    void set_watchpoint_callback(CpuWatchpointCallback cb) {
        watchpoint_callback_ = std::move(cb);
    }

    void set_instruction_callback(CpuInstructionCallback cb) {
        instruction_callback_ = std::move(cb);
    }

    void clear_watchpoint_callback() { watchpoint_callback_ = nullptr; }
    void clear_instruction_callback() { instruction_callback_ = nullptr; }

private:
    CpuWatchpointCallback watchpoint_callback_;
    CpuInstructionCallback instruction_callback_;

    void execute_cycle() {
        // Instruction callback at start of new instruction
        if (instruction_callback_ && M6502_IsAboutToExecute(&cpu)) {
            instruction_callback_(cpu.pc.w);
        }

        // Execute one CPU cycle
        (*cpu.tfn)(&cpu);

        // Memory access
        const uint16_t addr = cpu.abus.w;
        uint8_t value;
        if (cpu.read) {
            value = memory.read(addr);
            cpu.dbus = value;
        } else {
            value = cpu.dbus;
            memory.write(addr, value);
        }

        // Watchpoint callback (if set, caller handles address filtering)
        if (watchpoint_callback_) {
            watchpoint_callback_(addr, value, !cpu.read);
        }
    }
};

} // namespace beebium

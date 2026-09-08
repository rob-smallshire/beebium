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

#include "InstructionTrace.hpp"
#include "CoprocessorMemoryMap.hpp"
#include "TubeCoprocessorBackend.hpp"

#include <6502/6502.h>
#include <cstdint>

namespace beebium {

// CPU wrapper for the 6502 second processor coprocessor.
//
// Wires a Rockwell 65C02 to the CoprocessorMemoryMap for bus access and
// routes interrupt lines from the TubeCoprocessorBackend (PIRQ -> IRQ, PNMI -> NMI).
//
// The coprocessor CPU runs at 3 MHz with no bus stretching -- every tick
// is a single CPU cycle with a memory access.
//
// IRQ device mask bits:
//   bit 0: Tube PIRQ (the only IRQ source on the coprocessor)
//
// NMI device mask bits:
//   bit 0: Tube PNMI (the only NMI source on the coprocessor)

class CoprocessorCpu {
public:
    CoprocessorCpu(CoprocessorMemoryMap& memory, TubeCoprocessorBackend& tube_port);
    ~CoprocessorCpu();

    // Non-copyable (M6502 contains internal pointers)
    CoprocessorCpu(const CoprocessorCpu&) = delete;
    CoprocessorCpu& operator=(const CoprocessorCpu&) = delete;

    // Reset CPU, memory map, and Tube port. Clears cycle count.
    void reset();

    // Execute one CPU cycle (one bus access).
    void tick();

    // Execute cycles until the next instruction boundary.
    // Returns the number of cycles taken.
    uint64_t step_instruction();

    // Execute for the given number of cycles.
    void run(uint64_t cycles);

    // Cycle counter.
    uint64_t cycle_count() const { return cycle_count_; }

    // Interrupt handler tracking.
    bool in_nmi_handler() const { return in_nmi_handler_; }

    // CPU state access.
    M6502& cpu() { return cpu_; }
    const M6502& cpu() const { return cpu_; }

    // Instruction trace buffer.  Disabled by default; enable via
    // trace().set_enabled(true).  Records PC, registers, and opcode
    // at every instruction boundary with zero overhead when disabled.
    InstructionTrace& trace() { return trace_; }
    const InstructionTrace& trace() const { return trace_; }

    // Memory write watchpoint.  When a write hits this address and
    // tracing is enabled, a pseudo-entry is recorded with opcode=$FF,
    // PC=address, A=written value.  Set to 0xFFFF to disable.
    void set_watch_write_addr(uint16_t addr) { watch_write_addr_ = addr; }

    // Memory read watchpoint.  When a read hits this address and
    // tracing is enabled, a pseudo-entry is recorded with opcode=$FE,
    // PC=instruction PC (the instruction that caused the read),
    // A=value read.  Set to 0xFFFF to disable.
    void set_watch_read_addr(uint16_t addr) { watch_read_addr_ = addr; }

private:
    static constexpr M6502_DeviceIRQFlags kPirqMask = 1;
    static constexpr M6502_DeviceNMIFlags kPnmiMask = 1;

    CoprocessorMemoryMap& memory_;
    TubeCoprocessorBackend& tube_port_;
    M6502 cpu_;
    uint64_t cycle_count_ = 0;

    // NMI handler tracking: while inside an NMI handler, PNMI updates are
    // suppressed to prevent NMI nesting.  In the cross-process Tube model,
    // the host thread can write the next R3 byte before the coprocessor's NMI
    // handler completes, which would cause a new NMI edge during the
    // handler.  In real hardware, the host is limited to 2 MHz and disc
    // byte rate (~16-32 us), giving the coprocessor time to finish before new
    // data arrives.
    //
    // Entry: detected when M6502ReadType_Interrupt and it's an NMI (not IRQ).
    // Exit:  detected when an RTI instruction (opcode $40) is about to execute.
    bool in_nmi_handler_ = false;

    // Memory watchpoints (0xFFFF = disabled).
    uint16_t watch_write_addr_ = 0xFFFF;
    uint16_t watch_read_addr_ = 0xFFFF;

    // Instruction trace (not allocated until capacity is set; default 4M entries).
    InstructionTrace trace_;
};

}  // namespace beebium

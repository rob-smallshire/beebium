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

#include "beebium/extension/CpuDescriptor.hpp"

#include <6502/6502.h>

#include <cstddef>
#include <cstdint>

// The 6502 family's self-description, shared by every 6502 debug target: the
// host's own CPU and the 65C02/65C102 coprocessors all present the same
// register file and interrupt signals. Kept in one place so the descriptor and
// the index<->accessor mapping cannot drift between implementations.

namespace beebium {

// Register indices into cpu6502_descriptor().registers, in display order.
enum Cpu6502Register {
    CPU6502_REG_A = 0,
    CPU6502_REG_X = 1,
    CPU6502_REG_Y = 2,
    CPU6502_REG_SP = 3,
    CPU6502_REG_PC = 4,
    CPU6502_REG_P = 5,
};

// Signal indices into cpu6502_descriptor().signals.
enum Cpu6502Signal {
    CPU6502_SIGNAL_IRQ = 0,
    CPU6502_SIGNAL_NMI = 1,
};

// The single 6502 descriptor. Flag names are bit 0 first; bit 5 is unused.
inline const cpu::CpuDescriptor& cpu6502_descriptor() {
    static const cpu::CpuDescriptor descriptor = [] {
        cpu::CpuDescriptor d;
        d.family = "6502";
        d.address_bits = 16;
        d.little_endian = true;
        d.registers = {
            {"A", 8, cpu::RegisterRole::None, {}},
            {"X", 8, cpu::RegisterRole::None, {}},
            {"Y", 8, cpu::RegisterRole::None, {}},
            {"SP", 8, cpu::RegisterRole::StackPointer, {}},
            {"PC", 16, cpu::RegisterRole::ProgramCounter, {}},
            {"P", 8, cpu::RegisterRole::Flags,
             {"C", "Z", "I", "D", "B", "", "V", "N"}},
        };
        d.signals = {"IRQ", "NMI"};
        return d;
    }();
    return descriptor;
}

// Read a register by index from anything exposing the 6502 accessor shape
// (a()/x()/y()/sp()/pc()/p()) -- the host Machine and the coprocessor runner
// both do.
template <typename Cpu6502Accessors>
uint64_t cpu6502_register_value(size_t index, const Cpu6502Accessors& r) {
    switch (index) {
        case CPU6502_REG_A: return r.a();
        case CPU6502_REG_X: return r.x();
        case CPU6502_REG_Y: return r.y();
        case CPU6502_REG_SP: return r.sp();
        case CPU6502_REG_PC: return r.pc();
        case CPU6502_REG_P: return r.p();
        default: return 0;
    }
}

// Write a register by index, mirror of cpu6502_register_value.
template <typename Cpu6502Accessors>
void cpu6502_set_register_value(size_t index, Cpu6502Accessors& r, uint64_t value) {
    switch (index) {
        case CPU6502_REG_A: r.set_a(static_cast<uint8_t>(value)); break;
        case CPU6502_REG_X: r.set_x(static_cast<uint8_t>(value)); break;
        case CPU6502_REG_Y: r.set_y(static_cast<uint8_t>(value)); break;
        case CPU6502_REG_SP: r.set_sp(static_cast<uint8_t>(value)); break;
        case CPU6502_REG_PC: r.set_pc(static_cast<uint16_t>(value)); break;
        case CPU6502_REG_P: r.set_p(static_cast<uint8_t>(value)); break;
        default: break;
    }
}

// Compute a signal's state from the live 6502 core. NMI's in_handler carries
// the NMI-handler tracking and its pending is the latched edge; IRQ's pending
// is the asserted-and-unmasked condition. The device flag masks are
// Beebium-internal aggregator state, not CPU state, and are not reported.
inline cpu::SignalStateValue cpu6502_signal_state(
    size_t index, const M6502& processor, uint8_t p,
    bool in_nmi_handler, bool in_irq_handler) {
    cpu::SignalStateValue s;
    switch (index) {
        case CPU6502_SIGNAL_IRQ:
            s.asserted = processor.irq_flags != 0;
            s.pending = processor.irq_flags != 0 && !(p & 0x04);
            s.in_handler = in_irq_handler;
            break;
        case CPU6502_SIGNAL_NMI:
            s.asserted = processor.nmi_flags != 0;
            s.pending = processor.nmi_flags != 0;
            s.in_handler = in_nmi_handler;
            break;
        default:
            break;
    }
    return s;
}

}  // namespace beebium

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

#include <cstdint>
#include <string>
#include <vector>

// Plain C++ description of a CPU's programmer-visible register file and
// interrupt signals. It crosses the extension API the way MemoryRegionDescriptor
// does -- a proto-free value type -- and the service layer translates it to the
// CpuDescriptor / CpuState protos. A CPU debug target describes itself with
// this so the server and clients need not name a CPU family.

// These live in beebium::cpu, not beebium, so the plain value types do not
// clash with the identically named debugger.proto messages (beebium::CpuDescriptor
// etc.); the service layer translates between the two.
namespace beebium::cpu {

// The minimum roles clients need to render a register file: which register is
// the program counter, which the stack pointer, which the flags. More roles
// (segment registers, banked sets) are added when a family that needs them
// arrives, driven by a concrete example, not before.
enum class RegisterRole {
    None = 0,
    ProgramCounter = 1,
    StackPointer = 2,
    Flags = 3,
};

struct RegisterDescriptor {
    std::string name;                     // "A", "PC", "HL", "SP", "P"
    uint32_t width_bits = 0;
    RegisterRole role = RegisterRole::None;
    // For a Flags register: one entry per bit, bit 0 first, "" for an unused
    // bit. Empty for every other register.
    std::vector<std::string> flag_names;
};

struct CpuDescriptor {
    std::string family;                   // "6502", "z80", "6809", ...
    uint32_t address_bits = 0;            // 16, 24, 32
    bool little_endian = true;
    std::vector<RegisterDescriptor> registers;  // display order
    std::vector<std::string> signals;     // interrupt line names, e.g. "IRQ", "NMI"
};

// State of one interrupt signal, reported in descriptor.signals order.
struct SignalStateValue {
    bool asserted = false;    // the line is currently asserted
    bool pending = false;     // a taken interrupt is pending (family-defined)
    bool in_handler = false;  // the CPU is currently in this signal's handler
};

}  // namespace beebium::cpu

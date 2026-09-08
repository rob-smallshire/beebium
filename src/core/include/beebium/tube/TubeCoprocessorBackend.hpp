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

namespace beebium {

// Abstract interface for the coprocessor side of a Tube bridge.
//
// TubeUla (in-process) implements this interface, allowing CoprocessorMemoryMap,
// CoprocessorCpu, and CoprocessorRunner to work with any backend.
//
// The interface covers:
//   - Register read/write/peek for Tube offsets 0-7 (mirrored)
//   - Interrupt outputs: PIRQ (level), PNMI (level for M6502 edge detection)
//   - Reset
class TubeCoprocessorBackend {
public:
    virtual ~TubeCoprocessorBackend() = default;

    // Coprocessor-side register access (offsets 0-7, mirrored from &FEF8-&FEFF).
    virtual uint8_t coprocessor_read(uint8_t offset) = 0;
    virtual uint8_t coprocessor_peek(uint8_t offset) const = 0;
    virtual void coprocessor_write(uint8_t offset, uint8_t value) = 0;

    // PIRQ: level-sensitive interrupt output.
    // Active when (I=1 AND R1 H-to-P has data) OR (J=1 AND R4 H-to-P has data).
    virtual bool pirq() const = 0;

    // PNMI level: combinational NMI output.
    // Active when M=1 AND (R3 H-to-P has data OR R3 P-to-H has space).
    // The M6502 performs its own edge detection, so this is the raw level.
    virtual bool pnmi_level() const = 0;

    // Reset all register state.
    virtual void reset() = 0;
};

}  // namespace beebium

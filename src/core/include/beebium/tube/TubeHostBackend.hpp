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

class TubeInspection;

// Abstract host-side interface for the Tube ULA.
//
// Implemented by:
//   TubeUla          -- full in-process model (both host and coprocessor sides)
//   EmptyTubeBackend -- null object for an empty socket (no second processor)
//
// TubeSocket holds a unique_ptr<TubeHostBackend> and delegates all register
// access, IRQ queries, and reset through this interface.

class TubeHostBackend {
public:
    virtual ~TubeHostBackend() = default;

    // Host-side register read (offset 0-7, mirrored from &FEE0-&FEE7).
    virtual uint8_t host_read(uint8_t offset) = 0;

    // Side-effect-free read for debugger inspection.
    // Returns the same value as host_read() for status registers, but does not
    // dequeue FIFOs, clear ready flags, or update interrupt state.
    virtual uint8_t host_peek(uint8_t offset) const = 0;

    // Host-side register write.
    virtual void host_write(uint8_t offset, uint8_t value) = 0;

    // Host IRQ output (HIRQ). Active when Q=1 and R4 P-to-H has data.
    virtual bool hirq() const = 0;

    // Read-only diagnostic surface, or nullptr if this backend offers none.
    // DeviceInspectionService::GetTubeState fills from it when present, so the
    // Tube state it reports is identical whether the backend is the socket's
    // own in-process ULA or one installed by a coprocessor extension.
    virtual const TubeInspection* inspection() const { return nullptr; }

    // Full hardware reset (HRST).
    virtual void reset() = 0;
};

}  // namespace beebium

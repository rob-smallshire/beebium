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

#include <array>
#include <cstddef>
#include <cstdint>

namespace beebium {

// Read-only diagnostic surface of a Tube ULA (or an equivalent bridge),
// reachable from TubeHostBackend::inspection(). It is the whole of what
// DeviceInspectionService::GetTubeState reads, grouped behind one accessor so
// that the register-access interface Machine calls every cycle stays small and
// no code outside the coprocessor touches the concrete TubeUla.
//
// The register-bit constants and the TransferCounters and TraceEntry types
// live here, the single source of truth; TubeUla inherits them.
class TubeInspection {
public:
    virtual ~TubeInspection() = default;

    // Status flag bits (bits 7 and 6 of status register reads).
    static constexpr uint8_t DATA_AVAILABLE = 0x80;   // bit 7
    static constexpr uint8_t SPACE_AVAILABLE = 0x40;  // bit 6

    // Control flag bits (written via host offset 0).
    static constexpr uint8_t FLAG_S = 0x80;  // set/clear mode select
    static constexpr uint8_t FLAG_T = 0x40;  // soft reset (clear all registers)
    static constexpr uint8_t FLAG_P = 0x20;  // parasite reset
    static constexpr uint8_t FLAG_V = 0x10;  // two-byte mode for R3
    static constexpr uint8_t FLAG_M = 0x08;  // enable PNMI from R3
    static constexpr uint8_t FLAG_J = 0x04;  // enable PIRQ from R4
    static constexpr uint8_t FLAG_I = 0x02;  // enable PIRQ from R1
    static constexpr uint8_t FLAG_Q = 0x01;  // enable HIRQ from R4

    // Transfer counters: every byte written/read on each register direction,
    // accumulated across the session (reset only by hard reset).
    struct TransferCounters {
        uint64_t r1_h2p_writes = 0;
        uint64_t r1_h2p_reads = 0;
        uint64_t r2_h2p_writes = 0;
        uint64_t r2_h2p_reads = 0;
        uint64_t r3_h2p_writes = 0;
        uint64_t r3_h2p_reads = 0;
        uint64_t r4_h2p_writes = 0;
        uint64_t r4_h2p_reads = 0;
        uint64_t r1_p2h_writes = 0;
        uint64_t r1_p2h_reads = 0;
        uint64_t r2_p2h_writes = 0;
        uint64_t r2_p2h_reads = 0;
        uint64_t r3_p2h_writes = 0;
        uint64_t r3_p2h_reads = 0;
        uint64_t r4_p2h_writes = 0;
        uint64_t r4_p2h_reads = 0;

        void reset() { *this = TransferCounters{}; }
    };

    // Protocol trace ring buffer entry.
    // Tag encoding: bits 7-4 = register (1-4), bit 3 = direction (0=H2P, 1=P2H),
    //               bit 2 = side (0=host access, 1=parasite access).
    static constexpr size_t TRACE_SIZE = 1024;
    struct TraceEntry {
        uint8_t tag;    // register + direction + side
        uint8_t value;  // data byte
    };

    // Control flags (bits 0-5: Q, I, J, M, V, P).
    virtual uint8_t control_flags() const = 0;

    // Side-effect-free register reads from each side (offsets 0-7).
    virtual uint8_t host_peek(uint8_t offset) const = 0;
    virtual uint8_t parasite_peek(uint8_t offset) const = 0;

    // Interrupt outputs.
    virtual bool hirq() const = 0;
    virtual bool pirq() const = 0;
    virtual bool pnmi() const = 0;

    // Transfer counters.
    virtual const TransferCounters& counters() const = 0;

    // Snapshot the protocol trace into out (capacity max_entries), oldest
    // first. Returns the number of entries written.
    virtual size_t trace_snapshot(TraceEntry* out, size_t max_entries) const = 0;

    // Total trace events recorded this session (may exceed TRACE_SIZE; the
    // ring keeps only the most recent TRACE_SIZE).
    virtual size_t trace_count() const = 0;

    // True while the host CPU is stalled waiting for a register to drain.
    virtual bool stretched() const = 0;
};

}  // namespace beebium

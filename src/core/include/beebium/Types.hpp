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

#ifndef BEEBIUM_TYPES_HPP
#define BEEBIUM_TYPES_HPP

#include <cstdint>
#include <cstddef>
#include <optional>

#include "debugger/Expression.hpp"

namespace beebium {

// Memory region sizes
constexpr size_t kAddressSpaceSize = 65536;  // 64KB
constexpr size_t kRomBankSize = 16384;       // 16KB per ROM bank
constexpr size_t kNumRomBanks = 16;          // 16 sideways ROM banks

// BBC Model B memory map
constexpr uint16_t kRamStart = 0x0000;
constexpr uint16_t kRamEnd = 0x7FFF;         // 32KB RAM

constexpr uint16_t kRomStart = 0x8000;
constexpr uint16_t kRomEnd = 0xBFFF;         // Paged ROM area (16KB)

constexpr uint16_t kMosStart = 0xC000;
constexpr uint16_t kMosEnd = 0xFFFF;         // MOS ROM (16KB)

// SHEILA (hardware registers, memory-mapped I/O)
constexpr uint16_t kSheilaStart = 0xFE00;
constexpr uint16_t kSheilaEnd = 0xFEFF;

// Key I/O addresses
constexpr uint16_t kRomselAddr = 0xFE30;     // ROM bank select
constexpr uint16_t kSystemViaAddr = 0xFE40;  // System VIA base
constexpr uint16_t kUserViaAddr = 0xFE60;    // User VIA base

// Slot size constants
constexpr size_t kSlotSize = 16384;  // 16KB sideways ROM/RAM slot

// Watchpoint access types
enum WatchType : uint8_t {
    WATCH_READ  = 0x01,
    WATCH_WRITE = 0x02,
    WATCH_BOTH  = 0x03
};

// Debugger breakpoint entry -- checked inline in the tick loop at instruction boundaries.
// Sorted by start address. Modified only while the machine is stopped.
//
// Like watchpoints, breakpoints match a half-open address range [start, end).
// A single-address breakpoint is [addr, addr+1). A full-range breakpoint
// [0x0000, 0x10000) fires at every instruction boundary, useful with a
// condition like "cycles >= 10000000" for cycle-budget runs.
struct BreakpointEntry {
    uint32_t id;
    uint32_t start;
    uint32_t end;       // exclusive [start, end); 0x10000 for full address space
    bool stop_counterpart = false;

    // Condition expression (evaluated on address match).
    // Empty = unconditional (always stops). Uses the same expression syntax
    // as watchpoints, including the `hits` pseudo-variable.
    std::optional<CompiledExpression> condition{};

    // Hit counter: increments on every address match, available as `hits` in the condition.
    uint64_t hit_count = 0;

    bool matches(uint32_t pc) const {
        return pc >= start && pc < end;
    }
};

// Debugger watchpoint entry -- checked inline in CpuBinding on every bus access.
// Sorted by start address. Modified only while the machine is stopped.
struct WatchpointEntry {
    uint32_t id;
    uint32_t start;
    uint32_t end;       // exclusive [start, end); 0x10000 for full address space
    WatchType type;     // WATCH_READ, WATCH_WRITE, WATCH_BOTH
    bool stop_counterpart = false;

    // Condition expression (evaluated on address match).
    // Empty = unconditional (always stops). Condition "false" = never stops (recording only).
    // The expression can use the `hits` pseudo-variable for hit count logic,
    // e.g. "hits == 5" (exact), "hits % 10 == 0" (multiple), "hits > 3" (greater).
    std::optional<CompiledExpression> condition{};

    // Hit counter: increments on every address match, available as `hits` in the condition.
    uint64_t hit_count = 0;

    bool matches(uint16_t addr, bool is_write) const {
        if (addr < start || addr >= end) return false;
        if (is_write && (type & WATCH_WRITE)) return true;
        if (!is_write && (type & WATCH_READ)) return true;
        return false;
    }
};

} // namespace beebium

#endif // BEEBIUM_TYPES_HPP

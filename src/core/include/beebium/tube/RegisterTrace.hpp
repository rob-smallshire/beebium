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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace beebium {

// Trace entry for a Tube register access (read or write).
struct RegisterTraceEntry {
    uint64_t cycle;      // CPU cycle count (host or coprocessor, depending on side)
    uint8_t  offset;     // Register offset (0-7)
    uint8_t  value;      // Data value read or written
    uint8_t  direction;  // 0 = read, 1 = write
    uint8_t  pad[5];     // Padding to 16 bytes
};

static_assert(sizeof(RegisterTraceEntry) == 16,
    "RegisterTraceEntry must be exactly 16 bytes");

// Pre-allocated ring buffer for Tube register access tracing.
//
// Same design as InstructionTrace: zero overhead when disabled,
// single pointer bump per event, wraps when full.
class RegisterTrace {
public:
    RegisterTrace() = default;

    explicit RegisterTrace(size_t capacity)
        : entries_(capacity)
    {}

    void resize(size_t capacity) {
        entries_.resize(capacity);
        clear();
    }

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    void record(uint64_t cycle, uint8_t offset, uint8_t value, bool is_write) {
        if (!enabled_) return;
        auto& e = entries_[write_pos_];
        e.cycle = cycle;
        e.offset = offset;
        e.value = value;
        e.direction = is_write ? 1 : 0;
        if (++write_pos_ >= entries_.size())
            write_pos_ = 0;
        ++total_count_;
    }

    void clear() {
        write_pos_ = 0;
        total_count_ = 0;
    }

    uint64_t total_count() const { return total_count_; }

    size_t available() const {
        return total_count_ < entries_.size() ? static_cast<size_t>(total_count_) : entries_.size();
    }

    size_t capacity() const { return entries_.size(); }

    const RegisterTraceEntry& operator[](size_t index) const {
        size_t start = (total_count_ <= entries_.size()) ? 0 : write_pos_;
        return entries_[(start + index) % entries_.size()];
    }

    const std::vector<RegisterTraceEntry>& entries() const { return entries_; }
    size_t write_pos() const { return write_pos_; }

private:
    std::vector<RegisterTraceEntry> entries_;
    size_t write_pos_ = 0;
    uint64_t total_count_ = 0;
    bool enabled_ = false;
};

}  // namespace beebium

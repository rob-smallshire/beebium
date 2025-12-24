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

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace beebium {

// Program counter histogram for instruction execution profiling.
// Tracks how many times each address has been executed as an instruction.
//
// This is distinct from MemoryHistogram which tracks memory reads/writes.
// ProgramCounterHistogram answers: "How often was code at address X executed?"
//
// Designed for runtime attachment to a Machine with minimal overhead:
// - Single pointer check + array increment when enabled
// - Zero overhead when not attached
//
// Memory footprint: ~512KB (65536 addresses × 8-byte counters)
//
// Usage:
//   auto pc_hist = std::make_unique<ProgramCounterHistogram>();
//   pc_hist->attach(machine);
//   // ... run emulation ...
//   auto [hottest_pc, count] = pc_hist->max_visits();
//   pc_hist->detach(machine);
//
class ProgramCounterHistogram {
public:
    static constexpr size_t kAddressCount = 65536;

    ProgramCounterHistogram() = default;

    // Attach to a Machine (records PC each time an instruction begins)
    template<typename Machine>
    void attach(Machine& machine) {
        machine.set_pc_histogram(this);
    }

    // Detach from a Machine
    template<typename Machine>
    void detach(Machine& machine) {
        machine.set_pc_histogram(nullptr);
    }

    // Record a PC visit (called by Machine::step)
    void record(uint16_t pc) noexcept {
        ++visit_counts_[pc];
    }

    // Get visit count for an address
    uint64_t visits(uint16_t addr) const noexcept {
        return visit_counts_[addr];
    }

    // Get total instruction executions
    uint64_t total_visits() const noexcept {
        uint64_t sum = 0;
        for (auto count : visit_counts_) {
            sum += count;
        }
        return sum;
    }

    // Clear all counters
    void clear() noexcept {
        visit_counts_.fill(0);
    }

    // Direct access to counter array for bulk operations
    const std::array<uint64_t, kAddressCount>& visit_counts() const noexcept {
        return visit_counts_;
    }

    // Find the address with the most visits
    std::pair<uint16_t, uint64_t> max_visits() const noexcept {
        auto it = std::max_element(visit_counts_.begin(), visit_counts_.end());
        auto addr = static_cast<uint16_t>(std::distance(visit_counts_.begin(), it));
        return {addr, *it};
    }

    // Count addresses with any visits (unique PCs executed)
    size_t unique_addresses() const noexcept {
        size_t count = 0;
        for (auto visits : visit_counts_) {
            if (visits > 0) ++count;
        }
        return count;
    }

    // Get top N most visited addresses (for hot spot analysis)
    std::vector<std::pair<uint16_t, uint64_t>> top_addresses(size_t n) const {
        std::vector<std::pair<uint16_t, uint64_t>> result;
        result.reserve(kAddressCount);

        for (size_t i = 0; i < kAddressCount; ++i) {
            if (visit_counts_[i] > 0) {
                result.emplace_back(static_cast<uint16_t>(i), visit_counts_[i]);
            }
        }

        // Partial sort to get top N
        if (result.size() > n) {
            std::partial_sort(result.begin(), result.begin() + n, result.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
            result.resize(n);
        } else {
            std::sort(result.begin(), result.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; });
        }

        return result;
    }

    // Check if address has exceeded a visit threshold (for loop detection)
    bool exceeds_threshold(uint16_t addr, uint64_t threshold) const noexcept {
        return visit_counts_[addr] > threshold;
    }

    // Find first address exceeding threshold (for loop detection)
    // Returns {address, count} or {0, 0} if none found
    std::pair<uint16_t, uint64_t> find_exceeding_threshold(uint64_t threshold) const noexcept {
        for (size_t i = 0; i < kAddressCount; ++i) {
            if (visit_counts_[i] > threshold) {
                return {static_cast<uint16_t>(i), visit_counts_[i]};
            }
        }
        return {0, 0};
    }

private:
    std::array<uint64_t, kAddressCount> visit_counts_{};
};

} // namespace beebium

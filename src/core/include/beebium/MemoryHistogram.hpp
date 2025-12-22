#pragma once

#include "Types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace beebium {

// Memory access histogram for diagnostic purposes.
// Tracks 64-bit read and write counters for each of the 65536 addresses.
//
// Designed to be allocated on-demand at runtime and attached to a Machine
// via the watchpoint infrastructure.
//
// Memory footprint: ~1MB (65536 * 8 bytes * 2 arrays)
//
// Usage:
//   auto histogram = std::make_unique<MemoryHistogram>();
//   histogram->attach(machine);
//   // ... run emulation ...
//   // Detach by calling machine.clear_watchpoints() or let histogram go out of scope
//
class MemoryHistogram {
public:
    static constexpr size_t kAddressCount = 65536;

    MemoryHistogram() = default;

    // Attach this histogram to a Machine via watchpoints.
    // The histogram records all memory reads and writes.
    // Call machine.clear_watchpoints() to detach, or use detach().
    template<typename Machine>
    void attach(Machine& machine) {
        machine.add_watchpoint(0, kAddressCount, WATCH_BOTH,
            [this](uint16_t addr, uint8_t /*value*/, bool is_write, uint64_t /*cycle*/) {
                if (is_write) {
                    ++write_counts_[addr];
                } else {
                    ++read_counts_[addr];
                }
            });
    }

    // Record a read access (for manual use without watchpoints)
    void record_read(uint16_t addr) noexcept {
        ++read_counts_[addr];
    }

    // Record a write access (for manual use without watchpoints)
    void record_write(uint16_t addr) noexcept {
        ++write_counts_[addr];
    }

    // Get read count for an address
    uint64_t reads(uint16_t addr) const noexcept {
        return read_counts_[addr];
    }

    // Get write count for an address
    uint64_t writes(uint16_t addr) const noexcept {
        return write_counts_[addr];
    }

    // Get total accesses for an address
    uint64_t total(uint16_t addr) const noexcept {
        return read_counts_[addr] + write_counts_[addr];
    }

    // Get total reads across all addresses
    uint64_t total_reads() const noexcept {
        uint64_t sum = 0;
        for (auto count : read_counts_) {
            sum += count;
        }
        return sum;
    }

    // Get total writes across all addresses
    uint64_t total_writes() const noexcept {
        uint64_t sum = 0;
        for (auto count : write_counts_) {
            sum += count;
        }
        return sum;
    }

    // Clear all counters
    void clear() noexcept {
        read_counts_.fill(0);
        write_counts_.fill(0);
    }

    // Direct access to counter arrays for bulk operations
    const std::array<uint64_t, kAddressCount>& read_counts() const noexcept {
        return read_counts_;
    }

    const std::array<uint64_t, kAddressCount>& write_counts() const noexcept {
        return write_counts_;
    }

    // Find the address with the most reads
    std::pair<uint16_t, uint64_t> max_reads() const noexcept {
        auto it = std::max_element(read_counts_.begin(), read_counts_.end());
        auto addr = static_cast<uint16_t>(std::distance(read_counts_.begin(), it));
        return {addr, *it};
    }

    // Find the address with the most writes
    std::pair<uint16_t, uint64_t> max_writes() const noexcept {
        auto it = std::max_element(write_counts_.begin(), write_counts_.end());
        auto addr = static_cast<uint16_t>(std::distance(write_counts_.begin(), it));
        return {addr, *it};
    }

    // Find the address with the most total accesses
    std::pair<uint16_t, uint64_t> max_total() const noexcept {
        uint16_t max_addr = 0;
        uint64_t max_count = 0;
        for (size_t i = 0; i < kAddressCount; ++i) {
            uint64_t t = read_counts_[i] + write_counts_[i];
            if (t > max_count) {
                max_count = t;
                max_addr = static_cast<uint16_t>(i);
            }
        }
        return {max_addr, max_count};
    }

    // Count addresses with any accesses
    size_t active_addresses() const noexcept {
        size_t count = 0;
        for (size_t i = 0; i < kAddressCount; ++i) {
            if (read_counts_[i] > 0 || write_counts_[i] > 0) {
                ++count;
            }
        }
        return count;
    }

private:
    std::array<uint64_t, kAddressCount> read_counts_{};
    std::array<uint64_t, kAddressCount> write_counts_{};
};

} // namespace beebium

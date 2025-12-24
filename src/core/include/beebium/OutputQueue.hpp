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

#ifndef BEEBIUM_OUTPUT_QUEUE_HPP
#define BEEBIUM_OUTPUT_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cassert>
#include <span>

namespace beebium {

// Lock-free single-producer, single-consumer circular buffer.
//
// The producer calls get_producer_buffer() to get a span of writable slots,
// writes to them, then calls produce(n) to commit n items.
//
// The consumer calls get_consumer_buffer() to get a span of readable items,
// reads them, then calls consume(n) to release n slots.
//
// The buffer may wrap around, so both methods return up to two spans (A and B).
// Fill/consume A first, then B.
//
// Thread safety: One producer thread and one consumer thread may operate
// concurrently without external synchronization.
template <typename T>
class OutputQueue {
public:
    // Default capacity: ~256K items (~1 frame of video at 2MHz)
    static constexpr size_t DEFAULT_CAPACITY = 262144;

    explicit OutputQueue(size_t capacity = DEFAULT_CAPACITY)
        : capacity_(capacity)
        , buffer_(std::make_unique<T[]>(capacity)) {
    }

    ~OutputQueue() = default;

    // Non-copyable, non-movable (atomic members)
    OutputQueue(const OutputQueue&) = delete;
    OutputQueue& operator=(const OutputQueue&) = delete;
    OutputQueue(OutputQueue&&) = delete;
    OutputQueue& operator=(OutputQueue&&) = delete;

    // --- Producer interface ---

    // Get writable buffer space. Returns spans for A and B portions.
    // If no space available, both spans are empty.
    // Call produce() to commit written items.
    struct ProducerBuffers {
        std::span<T> a;  // First portion (or only portion)
        std::span<T> b;  // Second portion (wrap-around), may be empty
        size_t total() const { return a.size() + b.size(); }
        bool empty() const { return total() == 0; }
    };

    ProducerBuffers get_producer_buffer() {
        uint64_t read_pos = read_pos_.load(std::memory_order_acquire);
        uint64_t write_pos = write_pos_.load(std::memory_order_acquire);

        assert(write_pos >= read_pos);
        size_t used = static_cast<size_t>(write_pos - read_pos);
        size_t free = capacity_ - used;

        if (free == 0) {
            return {{}, {}};
        }

        size_t begin_idx = static_cast<size_t>(write_pos % capacity_);
        size_t end_idx = begin_idx + free;

        if (end_idx <= capacity_) {
            // Single contiguous region
            return {
                std::span<T>(buffer_.get() + begin_idx, free),
                {}
            };
        } else {
            // Wraps around
            return {
                std::span<T>(buffer_.get() + begin_idx, capacity_ - begin_idx),
                std::span<T>(buffer_.get(), end_idx - capacity_)
            };
        }
    }

    // Commit n items to the buffer (makes them available to consumer).
    void produce(size_t n) {
        write_pos_.fetch_add(n, std::memory_order_acq_rel);
    }

    // Convenience: write a single item (returns false if buffer full)
    bool push(const T& item) {
        auto bufs = get_producer_buffer();
        if (bufs.empty()) {
            return false;
        }
        bufs.a[0] = item;
        produce(1);
        return true;
    }

    // --- Consumer interface ---

    // Get readable items. Returns spans for A and B portions.
    // If no items available, both spans are empty.
    // Call consume() to release read items.
    struct ConsumerBuffers {
        std::span<const T> a;  // First portion
        std::span<const T> b;  // Second portion (wrap-around), may be empty
        size_t total() const { return a.size() + b.size(); }
        bool empty() const { return total() == 0; }
    };

    ConsumerBuffers get_consumer_buffer() const {
        uint64_t read_pos = read_pos_.load(std::memory_order_acquire);
        uint64_t write_pos = write_pos_.load(std::memory_order_acquire);

        assert(write_pos >= read_pos);
        size_t used = static_cast<size_t>(write_pos - read_pos);

        if (used == 0) {
            return {{}, {}};
        }

        size_t begin_idx = static_cast<size_t>(read_pos % capacity_);
        size_t end_idx = begin_idx + used;

        if (end_idx <= capacity_) {
            // Single contiguous region
            return {
                std::span<const T>(buffer_.get() + begin_idx, used),
                {}
            };
        } else {
            // Wraps around
            return {
                std::span<const T>(buffer_.get() + begin_idx, capacity_ - begin_idx),
                std::span<const T>(buffer_.get(), end_idx - capacity_)
            };
        }
    }

    // Release n items from the buffer (frees space for producer).
    void consume(size_t n) {
        read_pos_.fetch_add(n, std::memory_order_acq_rel);
    }

    // --- Query interface ---

    size_t capacity() const { return capacity_; }

    size_t size() const {
        uint64_t read_pos = read_pos_.load(std::memory_order_acquire);
        uint64_t write_pos = write_pos_.load(std::memory_order_acquire);
        return static_cast<size_t>(write_pos - read_pos);
    }

    size_t available() const {
        return capacity_ - size();
    }

    bool empty() const { return size() == 0; }
    bool full() const { return size() == capacity_; }

    // Reset buffer to empty state (not thread-safe - call only when idle)
    void reset() {
        read_pos_.store(0, std::memory_order_release);
        write_pos_.store(0, std::memory_order_release);
    }

private:
    const size_t capacity_;
    std::unique_ptr<T[]> buffer_;

    // Consumer state - cache line aligned to avoid false sharing
    alignas(64) std::atomic<uint64_t> read_pos_{0};

    // Producer state - separate cache line
    alignas(64) std::atomic<uint64_t> write_pos_{0};
};

} // namespace beebium

#endif // BEEBIUM_OUTPUT_QUEUE_HPP

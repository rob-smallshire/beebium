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
// writes to them, then calls produce(n) to commit n items. Or it commits
// them with stage(n), which leaves them in the producer's own view, and
// makes everything staged visible to the consumer at once with publish():
// one release store however many items, for a producer that commits items
// far more often than a consumer looks.
//
// The consumer calls get_consumer_buffer() to get a span of readable items,
// reads them, then calls consume(n) to release n slots.
//
// The buffer may wrap around, so both methods return up to two spans (A and B).
// Fill/consume A first, then B.
//
// Thread safety: One producer thread and one consumer thread may operate
// concurrently without external synchronization. The producer role may pass
// from one thread to another, and so may the consumer role, only with a
// handover that orders them (a mutex, a join).
//
// Memory ordering: each index has exactly one writer, so neither needs a
// read-modify-write. A side reads its own index relaxed (it wrote it), reads
// the other side's index with acquire, and publishes its own with a release
// store. The producer's release of write_pos_ makes the items it wrote
// visible to the consumer's acquire of write_pos_; the consumer's release of
// read_pos_ orders its reads of the released slots before the producer's
// acquire of read_pos_ and the overwrites that follow. On AArch64 these are
// plain LDAR/STLR, with no atomic read-modify-write (which GCC calls out of
// line) on a path that runs for every pixel batch.
//
// Cached indices: each side keeps a private copy of the other side's index
// and reloads it (with acquire) only when the copy says the ring is full
// (producer) or empty (consumer). Indices only grow, so a stale copy only
// understates: the producer is offered no more space than is really free,
// and the consumer no more items than are really written. A reload happens
// before either side reports full or empty, so neither does so falsely.
// This keeps each side off the other's cache line until it has to look:
// with the render thread consuming on another core, reading read_pos_ for
// every pixel batch was a coherence miss each time. size(), available(),
// empty() and full() read both indices afresh, for any thread's view.
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

    // Get writable buffer space. Returns spans for A and B portions, perhaps
    // less than is free (the producer's cached view; see above), and empty
    // only when the ring is full. Call produce() to commit written items.
    struct ProducerBuffers {
        std::span<T> a;  // First portion (or only portion)
        std::span<T> b;  // Second portion (wrap-around), may be empty
        size_t total() const { return a.size() + b.size(); }
        bool empty() const { return total() == 0; }
    };

    ProducerBuffers get_producer_buffer() {
        // Staged items count as used: the producer's view starts after them.
        uint64_t write_pos = staged_write_pos_;
        // Reload when the cached view shows no free slot (or, after a
        // produce() of more than this view offered, an impossible one).
        if (write_pos - cached_read_pos_ >= capacity_) {
            cached_read_pos_ = read_pos_.load(std::memory_order_acquire);
            assert(write_pos >= cached_read_pos_);
        }
        size_t free = capacity_ - static_cast<size_t>(write_pos - cached_read_pos_);
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
        stage(n);
        publish();
    }

    // Commit n items to the producer's view without showing them to the
    // consumer: they hold their slots, and publish() releases them.
    void stage(size_t n) {
        staged_write_pos_ += n;
    }

    // Make every staged item visible to the consumer, with one release
    // store. Does nothing when nothing is staged.
    void publish() {
        if (staged_write_pos_ != write_pos_.load(std::memory_order_relaxed)) {
            write_pos_.store(staged_write_pos_, std::memory_order_release);
        }
    }

    // Items staged but not yet published (producer thread only).
    size_t unpublished() const {
        return static_cast<size_t>(staged_write_pos_ - write_pos_.load(std::memory_order_relaxed));
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

    // Get readable items. Returns spans for A and B portions, perhaps fewer
    // than are written (the consumer's cached view; see above), and empty
    // only when the ring is empty. Call consume() to release read items.
    struct ConsumerBuffers {
        std::span<const T> a;  // First portion
        std::span<const T> b;  // Second portion (wrap-around), may be empty
        size_t total() const { return a.size() + b.size(); }
        bool empty() const { return total() == 0; }
    };

    ConsumerBuffers get_consumer_buffer() const {
        uint64_t read_pos = read_pos_.load(std::memory_order_relaxed);
        // Reload when the cached view shows nothing to read (or, after a
        // consume() of items this view never offered, less than nothing).
        if (cached_write_pos_ <= read_pos) {
            cached_write_pos_ = write_pos_.load(std::memory_order_acquire);
            assert(cached_write_pos_ >= read_pos);
        }
        size_t used = static_cast<size_t>(cached_write_pos_ - read_pos);
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
        const uint64_t read_pos = read_pos_.load(std::memory_order_relaxed);
        read_pos_.store(read_pos + n, std::memory_order_release);
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
        cached_read_pos_ = 0;
        cached_write_pos_ = 0;
        staged_write_pos_ = 0;
    }

private:
    const size_t capacity_;
    std::unique_ptr<T[]> buffer_;

    // Consumer state - cache line aligned to avoid false sharing. The
    // consumer's cached copy of write_pos_ is mutable because
    // get_consumer_buffer() is const: reloading it changes no observable state.
    alignas(64) std::atomic<uint64_t> read_pos_{0};
    mutable uint64_t cached_write_pos_ = 0;

    // Producer state - separate cache line, with its cached copy of read_pos_
    // and the position up to which it has staged items (write_pos_ is where
    // it last published).
    alignas(64) std::atomic<uint64_t> write_pos_{0};
    uint64_t cached_read_pos_ = 0;
    uint64_t staged_write_pos_ = 0;
};

} // namespace beebium

#endif // BEEBIUM_OUTPUT_QUEUE_HPP

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

#include "OutputQueue.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace beebium {

// Audio sample format: N × 32-bit fields for flexible multi-source audio
//
// Each 32-bit field can represent:
// - 4 × 8-bit channels (e.g., SN76489: [tone0|tone1|tone2|noise])
// - 2 × 16-bit channels (e.g., Music 5000 stereo pair: [left|right])
// - 1 × 32-bit channel (e.g., high-quality digital audio)
//
// Interpretation is provided via gRPC GetAudioFormat metadata API.
struct AudioSample {
    static constexpr size_t MAX_SOURCES = 4;  // Expandable as needed
    uint32_t sources[MAX_SOURCES];            // N × 32-bit fields

    AudioSample() : sources{0, 0, 0, 0} {}

    // Pack 4 × 8-bit signed channels into sources[index] (legacy zero-centered)
    void pack_4x8bit_signed(size_t index, int8_t ch0, int8_t ch1, int8_t ch2, int8_t ch3) {
        sources[index] = (static_cast<uint32_t>(static_cast<uint8_t>(ch0)) << 24) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(ch1)) << 16) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(ch2)) << 8) |
                        static_cast<uint32_t>(static_cast<uint8_t>(ch3));
    }

    // Pack 4 × 8-bit unsigned channels into sources[index] (DC bias pre-applied)
    // Used for SN76489: 0-255 normalized samples with DC bias baked in
    void pack_4x8bit_unsigned(size_t index, uint8_t ch0, uint8_t ch1, uint8_t ch2, uint8_t ch3) {
        sources[index] = (static_cast<uint32_t>(ch0) << 24) |
                        (static_cast<uint32_t>(ch1) << 16) |
                        (static_cast<uint32_t>(ch2) << 8) |
                        static_cast<uint32_t>(ch3);
    }

    // Legacy alias for signed packing
    void pack_4x8bit(size_t index, int8_t ch0, int8_t ch1, int8_t ch2, int8_t ch3) {
        pack_4x8bit_signed(index, ch0, ch1, ch2, ch3);
    }

    // Pack 2 × 16-bit signed channels (for stereo sources)
    void pack_2x16bit(size_t index, int16_t left, int16_t right) {
        uint16_t u_left = static_cast<uint16_t>(left);
        uint16_t u_right = static_cast<uint16_t>(right);

        sources[index] = (static_cast<uint32_t>(u_right) << 16) |
                        static_cast<uint32_t>(u_left);
    }

    // Pack 1 × 32-bit signed channel
    void pack_1x32bit(size_t index, int32_t value) {
        sources[index] = static_cast<uint32_t>(value);
    }
};

// Circular buffer for audio samples (wraps OutputQueue)
//
// Producer (core thread): pushes samples as they're generated
// Consumer (gRPC thread): reads chunks for streaming
//
// Capacity: ~1 second @ 48 kHz = 48000 samples × 16 bytes = 768 KB
class AudioBuffer {
public:
    static constexpr size_t DEFAULT_CAPACITY = 48000;  // 1 second @ 48 kHz

    explicit AudioBuffer(size_t capacity = DEFAULT_CAPACITY)
        : queue_(capacity), sequence_(0), produced_(0), consumed_(0),
          last_read_index_(0), gen_ring_(capacity + 1) {}

    // --- Producer interface (core thread) ---

    // Push a single sample (returns false if buffer full).
    //
    // `produced_` counts every generated sample, whether kept or dropped, so it
    // is the sample's index in the full produced stream. Each kept sample's
    // produced index is recorded in a parallel ring; because drops only occur at
    // the tail when the buffer is full, that index is the only way the consumer
    // can see the gap a drop leaves.
    bool push(const AudioSample& sample) {
        if (queue_.push(sample)) {
            gen_ring_[sequence_ % gen_ring_.size()] = produced_;
            sequence_++;
            produced_++;
            return true;
        }
        produced_++;  // dropped: counted, but not delivered
        return false;
    }

    // Get producer buffer for batch writes
    auto get_producer_buffer() { return queue_.get_producer_buffer(); }

    // Commit n samples (after writing to producer buffer). The batch path
    // reserves space first, so these are never dropped.
    void produce(size_t n) {
        queue_.produce(n);
        for (size_t i = 0; i < n; ++i) {
            gen_ring_[(sequence_ + i) % gen_ring_.size()] = produced_ + i;
        }
        sequence_ += n;
        produced_ += n;
    }

    // --- Consumer interface (gRPC thread) ---

    // Read up to max_count samples into dest buffer
    // Returns actual number of samples read
    size_t read(AudioSample* dest, size_t max_count) {
        auto bufs = queue_.get_consumer_buffer();
        size_t available = bufs.total();
        size_t to_read = (available < max_count) ? available : max_count;

        // Produced index of the first sample this read returns, captured before
        // the consume cursor advances.
        if (to_read > 0) {
            last_read_index_ = gen_ring_[consumed_ % gen_ring_.size()];
        }

        size_t read_count = 0;

        // Copy from buffer A
        if (!bufs.a.empty()) {
            size_t a_count = (bufs.a.size() < to_read) ? bufs.a.size() : to_read;
            for (size_t i = 0; i < a_count; ++i) {
                dest[i] = bufs.a[i];
            }
            read_count += a_count;
        }

        // Copy from buffer B (if needed and available)
        if (read_count < to_read && !bufs.b.empty()) {
            size_t b_count = to_read - read_count;
            for (size_t i = 0; i < b_count; ++i) {
                dest[read_count + i] = bufs.b[i];
            }
            read_count += b_count;
        }

        queue_.consume(read_count);
        consumed_ += read_count;
        return read_count;
    }

    // --- Query interface ---

    size_t capacity() const { return queue_.capacity(); }
    size_t available() const { return queue_.size(); }
    bool empty() const { return queue_.empty(); }
    uint64_t sequence() const { return sequence_; }

    // Total samples generated, including any dropped when the buffer was full.
    uint64_t produced() const { return produced_; }
    // Total samples dropped because the buffer was full.
    uint64_t dropped() const { return produced_ - sequence_; }
    // Produced index of the first sample returned by the most recent read(). A
    // consumer that sees this jump by more than the previous chunk's length has
    // detected dropped samples.
    uint64_t last_read_index() const { return last_read_index_; }

    void reset() {
        queue_.reset();
        sequence_ = 0;
        produced_ = 0;
        consumed_ = 0;
        last_read_index_ = 0;
    }

private:
    OutputQueue<AudioSample> queue_;
    uint64_t sequence_;         // Total samples kept (successfully pushed)
    uint64_t produced_;         // Total samples generated (kept + dropped)
    uint64_t consumed_;         // Total samples read out
    uint64_t last_read_index_;  // Produced index of the last read()'s first sample
    std::vector<uint64_t> gen_ring_;  // Per-kept-sample produced index
};

} // namespace beebium

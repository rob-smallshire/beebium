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

#ifndef BEEBIUM_SERVICE_AUDIO_FAN_OUT_HPP
#define BEEBIUM_SERVICE_AUDIO_FAN_OUT_HPP

#include "beebium/AudioBuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace beebium {
namespace service {

// Shares one AudioBuffer among any number of audio streams (#236).
//
// The AudioBuffer has one consumer. The fan-out is it: drain() reads the
// buffer under the fan-out's mutex, so only one thread reads it at a time,
// and appends what it read to every subscriber's own queue. Each subscriber
// takes chunks from its queue alone, so streams never take each other's
// samples. A subscriber whose queue is full drops what it cannot hold and
// counts it; no other subscriber, and never the producer, waits for it.
//
// Each queue keeps the produced index of its samples: the index in the
// AudioBuffer's stream (delivered plus dropped there), so a chunk's index is
// its first sample's place in the one stream every subscriber shares. A chunk
// never spans a gap this subscriber's queue made, so a gap between chunks is
// exactly what it dropped (the buffer attributes its own drops to the first
// read after them, as it does for one consumer). A subscriber joining late
// starts at the current position.
class AudioFanOut {
public:
    // A subscriber's queue holds this many samples, as the AudioBuffer does.
    static constexpr size_t DEFAULT_CAPACITY = AudioBuffer::DEFAULT_CAPACITY;

    class Subscriber {
    public:
        explicit Subscriber(size_t capacity) : capacity_(capacity) {}

        // Samples this subscriber dropped because its queue was full.
        uint64_t dropped() const {
            std::lock_guard<std::mutex> lock(*mutex_);
            return dropped_;
        }

    private:
        friend class AudioFanOut;

        // A run of samples consecutive in the produced stream.
        struct Segment {
            uint64_t first_index;
            std::vector<AudioSample> samples;
            size_t offset = 0;  // samples already taken from the front
        };

        void append(uint64_t first_index, const AudioSample* samples, size_t count) {
            const size_t kept = std::min(count, capacity_ - queued_);
            dropped_ += count - kept;
            if (kept == 0) return;
            if (!segments_.empty()) {
                Segment& last = segments_.back();
                if (last.first_index + last.samples.size() == first_index) {
                    last.samples.insert(last.samples.end(), samples, samples + kept);
                    queued_ += kept;
                    return;
                }
            }
            segments_.push_back({first_index, std::vector<AudioSample>(samples, samples + kept)});
            queued_ += kept;
        }

        // Take up to max_count consecutive samples from the front segment. A
        // chunk ends where the segment does, at a gap (append() merges a run
        // that continues the last segment, so the next one starts after a
        // gap), so its first index places every sample in it.
        size_t take(AudioSample* dest, size_t max_count, uint64_t& first_index) {
            if (segments_.empty()) return 0;
            Segment& front = segments_.front();
            first_index = front.first_index + front.offset;
            const size_t n = std::min(max_count, front.samples.size() - front.offset);
            std::copy_n(front.samples.begin() + static_cast<std::ptrdiff_t>(front.offset), n, dest);
            front.offset += n;
            if (front.offset == front.samples.size()) segments_.pop_front();
            queued_ -= n;
            return n;
        }

        const size_t capacity_;
        std::mutex* mutex_ = nullptr;  // the fan-out's, which guards the rest
        std::deque<Segment> segments_;
        size_t queued_ = 0;
        uint64_t dropped_ = 0;
    };

    explicit AudioFanOut(size_t subscriber_capacity = DEFAULT_CAPACITY)
        : subscriber_capacity_(subscriber_capacity) {}

    AudioFanOut(const AudioFanOut&) = delete;
    AudioFanOut& operator=(const AudioFanOut&) = delete;

    std::shared_ptr<Subscriber> subscribe() {
        auto subscriber = std::make_shared<Subscriber>(subscriber_capacity_);
        subscriber->mutex_ = &mutex_;
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.push_back(subscriber);
        return subscriber;
    }

    void unsubscribe(const std::shared_ptr<Subscriber>& subscriber) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(std::remove(subscribers_.begin(), subscribers_.end(), subscriber),
                           subscribers_.end());
    }

    // Drain `buffer` into every subscriber's queue, then take up to
    // `max_count` samples from `subscriber`'s, if it holds at least
    // `min_count`. Returns the count taken (0 if fewer were queued) and sets
    // `first_index` to the first one's produced index.
    size_t drain_and_take(AudioBuffer& buffer, Subscriber& subscriber, AudioSample* dest,
                          size_t min_count, size_t max_count, uint64_t& first_index) {
        std::lock_guard<std::mutex> lock(mutex_);
        drain_locked(buffer);
        if (subscriber.queued_ < min_count) return 0;
        return subscriber.take(dest, max_count, first_index);
    }

private:
    void drain_locked(AudioBuffer& buffer) {
        while (true) {
            const size_t count = buffer.read(scratch_.data(), scratch_.size());
            if (count == 0) return;
            const uint64_t first_index = buffer.last_read_index();
            for (const auto& subscriber : subscribers_) {
                subscriber->append(first_index, scratch_.data(), count);
            }
        }
    }

    const size_t subscriber_capacity_;
    std::mutex mutex_;
    std::vector<std::shared_ptr<Subscriber>> subscribers_;
    std::vector<AudioSample> scratch_ = std::vector<AudioSample>(4096);
};

}  // namespace service
}  // namespace beebium

#endif  // BEEBIUM_SERVICE_AUDIO_FAN_OUT_HPP

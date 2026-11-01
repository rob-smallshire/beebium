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

#ifndef BEEBIUM_LINUX_AUDIO_FRAME_RING_HPP
#define BEEBIUM_LINUX_AUDIO_FRAME_RING_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace beebium::linux_client {

// The jitter buffer between the audio stream's thread and the playback
// thread (client specification 8.3): single producer, single consumer, lock
// free. Its element is a whole wire frame, every field together, so the
// fields cannot fall out of step: an overflow drops whole frames, and frame
// i of the SN76489 is always frame i of a Music 5000. The producer is never
// blocked; what does not fit is dropped and counted.
class FrameRing {
public:
    FrameRing(std::size_t capacity_frames, std::size_t fields_per_frame);

    // Producer: append up to `count` frames, each `fields()` words; returns
    // how many fitted.
    std::size_t write(const std::uint32_t* frames, std::size_t count);
    // Consumer: take up to `count` frames; a short read is an underrun.
    std::size_t read(std::uint32_t* destination, std::size_t count);

    std::size_t available() const;
    std::size_t capacity() const { return capacity_; }
    std::size_t fields() const { return fields_; }

    // Frames that never played because the ring was full while playing.
    std::uint64_t dropped_frames() const { return dropped_.load(std::memory_order_relaxed); }
    // Frames refused before the consumer first read: a sink slow to start,
    // not a glitch.
    std::uint64_t discarded_before_playback() const {
        return discarded_before_playback_.load(std::memory_order_relaxed);
    }
    // Consumer: take and discard up to `count` frames.
    std::size_t skip(std::size_t count);

    // The ring's own count of frames kept and of frames taken: the index the
    // next kept frame gets, and the index of the next to be read.
    std::uint64_t write_position() const { return write_index_.load(std::memory_order_acquire); }
    std::uint64_t read_position() const { return read_index_.load(std::memory_order_acquire); }
    std::uint64_t underrun_frames() const { return underrun_frames_.load(std::memory_order_relaxed); }
    std::uint64_t underrun_events() const { return underrun_events_.load(std::memory_order_relaxed); }

private:
    std::size_t capacity_;
    std::size_t fields_;
    std::vector<std::uint32_t> words_;
    std::atomic<std::uint64_t> read_index_{0};
    std::atomic<std::uint64_t> write_index_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> discarded_before_playback_{0};
    std::atomic<bool> consuming_{false};
    std::atomic<std::uint64_t> underrun_frames_{0};
    std::atomic<std::uint64_t> underrun_events_{0};
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_AUDIO_FRAME_RING_HPP

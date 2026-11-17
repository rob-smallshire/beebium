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

#include "FrontEnd.hpp"
#include "SampledSource.hpp"
#include "TapeSignal.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace beebium {

// Live sound from a client's audio input: a SampledSource fed through a
// lock-free ring by one producer (the gRPC thread streaming it) and drained by
// one consumer (the emulation thread), a sample a sample period of emulated
// time. The ring holds RING_SECONDS at the stream's rate.
//
// Neither end waits. The consumer starts by priming: it takes silence, not
// counted, until the ring holds PRIME_SECONDS of samples, a cushion against
// the jitter of the stream's arrival (a quarter of a second of latency between
// the deck and the machine). Then it takes a sample a sample period. When the
// ring runs empty it gets silence and counts each sample it lacked (an
// underrun) until the ring has primed again. When the ring is full the
// producer drops the oldest samples to make room and counts them (an
// overrun), so the newest are kept. The priming level is a Decision with the
// front end's constants. There is no resampling and no clock recovery: at real speed
// the host's clock and emulated time drift by parts per million and the ring
// absorbs it for hours; below real speed it overruns, above it underruns.
//
// The ring's read index moves by compare-and-swap, so the producer can drop
// the oldest samples (moving it forward past those it is about to overwrite)
// while the consumer reads; a consumer whose claim loses that race discards
// the sample it read. Slots are atomic, so a slot overwritten while it is read
// is a race the claim settles, not a torn value.
class LiveSource final : public SampledSource {
public:
    static constexpr double RING_SECONDS = 2.0;
    static constexpr double PRIME_SECONDS = 0.25;

    explicit LiveSource(uint32_t rate);

    // --- The producer's side ---

    // Push samples, dropping the oldest in the ring if they do not fit.
    void push(std::span<const float> samples);

    // --- The consumer's side ---

    // The next sample, or silence (counted) if none has arrived.
    float next_sample();

    // SampledSource: `read` takes samples as next_sample() does. A live
    // source has no length and cannot seek.
    size_t read(std::span<float> out) override;
    void seek(uint64_t) override {}
    uint64_t sample_count() const override { return 0; }
    uint32_t rate() const override { return rate_; }

    // --- Either side ---

    // How full the ring is, 0 to 1.
    double fill() const;
    uint64_t received() const { return received_.load(std::memory_order_relaxed); }
    uint64_t overruns() const { return overruns_.load(std::memory_order_relaxed); }
    uint64_t underruns() const { return underruns_.load(std::memory_order_relaxed); }
    uint64_t consumed() const { return consumed_.load(std::memory_order_relaxed); }

    // The stream has ended: what the ring holds plays out without priming,
    // and an empty ring is the end of the tape, not an underrun.
    void end_stream() { ended_.store(true, std::memory_order_release); }
    bool ended() const { return ended_.load(std::memory_order_acquire); }
    // Whether the ring is empty.
    bool empty() const { return write_.load(std::memory_order_acquire) <= read_.load(std::memory_order_acquire); }

    // The deck has let the tape go (an eject): the stream should end.
    void close() { closed_.store(true, std::memory_order_release); }
    bool closed() const { return closed_.load(std::memory_order_acquire); }

    size_t capacity() const { return capacity_; }
    size_t prime_level() const { return prime_; }
    // Whether the consumer is waiting for the ring to prime.
    bool priming() const { return priming_.load(std::memory_order_relaxed); }

private:
    uint32_t rate_;
    size_t capacity_;            // samples the ring holds
    size_t prime_;               // samples the ring holds before the consumer takes them
    size_t mask_;                // slots - 1; slots is the power of two at or above capacity
    std::vector<std::atomic<float>> slots_;
    std::atomic<uint64_t> write_{0};  // samples written, ever
    std::atomic<uint64_t> read_{0};   // samples consumed or dropped, ever

    std::atomic<uint64_t> received_{0};
    std::atomic<uint64_t> overruns_{0};
    std::atomic<uint64_t> underruns_{0};
    std::atomic<uint64_t> consumed_{0};
    std::atomic<bool> closed_{false};
    std::atomic<bool> ended_{false};
    std::atomic<bool> priming_{true};   // written by the consumer only
    bool starved_ = false;              // priming again after an underrun (consumer only)
};

// Streamed audio bytes to mono samples: little-endian signed 16-bit or 32-bit
// float frames of one or two channels, one channel taken. A frame split
// between two chunks is kept until the rest arrives.
class AudioDecoder {
public:
    enum class Format : uint8_t { S16, F32 };

    AudioDecoder(Format format, unsigned channels, unsigned channel)
        : format_(format), channel_(channel),
          frame_bytes_((format == Format::S16 ? 2u : 4u) * channels) {}

    // Append the samples in `bytes` to `out`.
    void decode(std::span<const uint8_t> bytes, std::vector<float>& out);

private:
    float sample_at(const uint8_t* frame) const;

    Format format_;
    unsigned channel_;
    unsigned frame_bytes_;
    std::vector<uint8_t> partial_;
};

// A live tape: a LiveSource squared by the FrontEnd. It is paced by emulated
// time, not read ahead: each next() takes one sample and returns its edge, if
// the front end flips within it, or a gap to its time, so the deck asks for
// the next sample only when emulated time reaches this one. It has no end and
// no length, cannot seek, and cannot be recorded on. Its kind is Live.
class LiveTape final : public TapeSignal {
public:
    LiveTape(std::shared_ptr<LiveSource> source, std::string channel);
    ~LiveTape() override;

    TapeItem next() override;
    void seek(TapeTime) override {}
    std::optional<TapeTime> duration() const override { return std::nullopt; }
    TapeKind kind() const override { return TapeKind::Live; }
    const TapeImageMetadata& metadata() const override { return metadata_; }

    LiveSource& source() { return *source_; }

private:
    TapeTime time_of(double samples) const;

    std::shared_ptr<LiveSource> source_;
    TapeImageMetadata metadata_;
    FrontEnd front_end_;
    uint64_t index_ = 0;  // the next sample's index since the stream began
    bool started_ = false;
};

}  // namespace beebium

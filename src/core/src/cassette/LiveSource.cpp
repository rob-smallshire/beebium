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

#include "beebium/cassette/LiveSource.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace beebium {

LiveSource::LiveSource(uint32_t rate)
    : rate_(rate),
      capacity_(static_cast<size_t>(RING_SECONDS * rate)),
      prime_(static_cast<size_t>(PRIME_SECONDS * rate)),
      mask_(std::bit_ceil(capacity_) - 1),
      slots_(mask_ + 1) {}

void LiveSource::push(std::span<const float> samples) {
    received_.fetch_add(samples.size(), std::memory_order_relaxed);
    // More than the ring holds at once: only the newest can stay.
    if (samples.size() > capacity_) {
        const size_t skipped = samples.size() - capacity_;
        overruns_.fetch_add(skipped, std::memory_order_relaxed);
        samples = samples.subspan(skipped);
    }
    const uint64_t w = write_.load(std::memory_order_relaxed);
    // Make room: move the read index past the oldest samples these overwrite.
    if (w + samples.size() > capacity_) {
        const uint64_t need = w + samples.size() - capacity_;
        uint64_t r = read_.load(std::memory_order_acquire);
        while (r < need) {
            if (read_.compare_exchange_weak(r, need, std::memory_order_acq_rel)) {
                overruns_.fetch_add(need - r, std::memory_order_relaxed);
                break;
            }
        }
    }
    for (size_t i = 0; i < samples.size(); ++i) {
        slots_[(w + i) & mask_].store(samples[i], std::memory_order_relaxed);
    }
    write_.store(w + samples.size(), std::memory_order_release);
}

float LiveSource::next_sample() {
    consumed_.fetch_add(1, std::memory_order_relaxed);
    uint64_t r = read_.load(std::memory_order_acquire);
    const bool ended = ended_.load(std::memory_order_acquire);
    if (priming_.load(std::memory_order_relaxed) && !ended) {
        if (write_.load(std::memory_order_acquire) - r < prime_) {
            // Silence while the ring primes: counted only after a starve.
            if (starved_) underruns_.fetch_add(1, std::memory_order_relaxed);
            return 0.0f;
        }
        priming_.store(false, std::memory_order_relaxed);
    }
    for (;;) {
        if (r >= write_.load(std::memory_order_acquire)) {
            if (ended) return 0.0f;  // the end of the tape
            // Starved: silence, counted, until the ring has primed again.
            underruns_.fetch_add(1, std::memory_order_relaxed);
            starved_ = true;
            priming_.store(true, std::memory_order_relaxed);
            return 0.0f;
        }
        const float value = slots_[r & mask_].load(std::memory_order_relaxed);
        // The claim settles a race with a producer dropping this sample.
        if (read_.compare_exchange_weak(r, r + 1, std::memory_order_acq_rel)) return value;
    }
}

size_t LiveSource::read(std::span<float> out) {
    for (float& sample : out) sample = next_sample();
    return out.size();
}

double LiveSource::fill() const {
    const uint64_t w = write_.load(std::memory_order_acquire);
    const uint64_t r = read_.load(std::memory_order_acquire);
    return w > r ? std::min(1.0, static_cast<double>(w - r) / static_cast<double>(capacity_)) : 0.0;
}

float AudioDecoder::sample_at(const uint8_t* frame) const {
    const uint8_t* p = frame + channel_ * (format_ == Format::S16 ? 2 : 4);
    if (format_ == Format::S16) return static_cast<int16_t>(p[0] | (p[1] << 8)) / 32768.0f;
    float f;
    std::memcpy(&f, p, 4);
    return f;
}

void AudioDecoder::decode(std::span<const uint8_t> bytes, std::vector<float>& out) {
    if (partial_size_ > 0) {
        const size_t take = std::min<size_t>(frame_bytes_ - partial_size_, bytes.size());
        std::copy_n(bytes.begin(), take, partial_.begin() + static_cast<std::ptrdiff_t>(partial_size_));
        partial_size_ += take;
        bytes = bytes.subspan(take);
        if (partial_size_ < frame_bytes_) return;
        out.push_back(sample_at(partial_.data()));
        partial_size_ = 0;
    }
    const size_t frames = bytes.size() / frame_bytes_;
    out.reserve(out.size() + frames);
    for (size_t i = 0; i < frames; ++i) out.push_back(sample_at(bytes.data() + i * frame_bytes_));
    partial_size_ = bytes.size() - frames * frame_bytes_;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(frames * frame_bytes_), partial_size_, partial_.begin());
}

LiveTape::LiveTape(std::shared_ptr<LiveSource> source, std::string channel)
    : source_(std::move(source)), front_end_(static_cast<double>(source_->rate())) {
    metadata_.sample_rate = source_->rate();
    metadata_.channel = std::move(channel);
}

LiveTape::~LiveTape() { source_->close(); }

TapeTime LiveTape::time_of(double samples) const {
    if (samples < 0.0) samples = 0.0;
    return static_cast<TapeTime>(samples * static_cast<double>(ULA_CLOCK_NUMERATOR) /
                                 (static_cast<double>(ULA_CLOCK_DENOMINATOR) * source_->rate()));
}

TapeItem LiveTape::next() {
    // The deck looks one item ahead when a tape goes in, before any sample
    // can have arrived: that look costs no sample.
    if (!started_) {
        started_ = true;
        return TapeItem::gap(0);
    }
    const uint64_t n = index_++;
    if (const auto f = front_end_.step(source_->next_sample())) {
        return TapeItem::edge(time_of(static_cast<double>(n) - 1.0 + *f), front_end_.high());
    }
    return TapeItem::gap(time_of(static_cast<double>(n)));
}

}  // namespace beebium

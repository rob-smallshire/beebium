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

#include "beebium/cassette/SampledTape.hpp"

namespace beebium {

SampledTape::SampledTape(std::unique_ptr<SampledSource> source, TapeKind kind)
    : source_(std::move(source)), kind_(kind), front_end_(static_cast<double>(source_->rate())),
      block_(BLOCK_SAMPLES) {
    metadata_.sample_rate = source_->rate();
}

TapeTime SampledTape::time_of(double samples) const {
    if (samples < 0.0) samples = 0.0;
    return static_cast<TapeTime>(samples * static_cast<double>(ULA_CLOCK_NUMERATOR) /
                                 (static_cast<double>(ULA_CLOCK_DENOMINATOR) * source_->rate()));
}

TapeItem SampledTape::next() {
    for (;;) {
        if (block_pos_ >= block_size_) {
            block_size_ = source_->read(block_);
            block_pos_ = 0;
            if (block_size_ == 0) return TapeItem::end(*duration());
        }
        const double x = block_[block_pos_++];
        const uint64_t n = index_++;
        if (const auto f = front_end_.step(x)) {
            // Between sample n - 1 and sample n.
            return TapeItem::edge(time_of(static_cast<double>(n) - 1.0 + *f), front_end_.high());
        }
    }
}

void SampledTape::seek(TapeTime time) {
    const auto index = static_cast<uint64_t>(static_cast<double>(time) * ULA_CLOCK_DENOMINATOR *
                                             source_->rate() / static_cast<double>(ULA_CLOCK_NUMERATOR));
    source_->seek(index);
    index_ = std::min(index, source_->sample_count());
    block_pos_ = block_size_ = 0;
    front_end_.reset();
}

std::optional<TapeTime> SampledTape::duration() const {
    return time_of(static_cast<double>(source_->sample_count()));
}

}  // namespace beebium

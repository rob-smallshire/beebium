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

#include <memory>
#include <vector>

namespace beebium {

// A tape that is sound: a SampledSource squared by the FrontEnd into edges.
// next() pulls samples until the front end flips or the source ends. A sample's
// tape time is its index x (16 MHz / 13) / rate, and an edge falls between two
// samples where the front end interpolates it. A seek starts the front end
// afresh, at rest. It cannot be recorded on.
class SampledTape final : public TapeSignal {
public:
    SampledTape(std::unique_ptr<SampledSource> source, TapeKind kind);

    TapeItem next() override;
    void seek(TapeTime time) override;
    std::optional<TapeTime> duration() const override;
    TapeKind kind() const override { return kind_; }
    const TapeImageMetadata& metadata() const override { return metadata_; }

    SampledSource& source() { return *source_; }
    TapeImageMetadata& mutable_metadata() { return metadata_; }

    // Samples pulled from the source at a time.
    static constexpr size_t BLOCK_SAMPLES = 4096;

private:
    TapeTime time_of(double samples) const;

    std::unique_ptr<SampledSource> source_;
    TapeKind kind_;
    TapeImageMetadata metadata_;
    FrontEnd front_end_;
    std::vector<float> block_;
    size_t block_pos_ = 0;
    size_t block_size_ = 0;
    uint64_t index_ = 0;  // the index of the next sample to step
};

}  // namespace beebium

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

#include "TapeSignal.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace beebium {

// A CSW (Compressed Square Wave) tape image, version 1.01 or 2, RLE or Z-RLE.
//
// Each pulse is a half-cycle of the squared signal, a length in samples; the
// signal starts at the level the header's polarity flag gives and toggles at
// the start of every pulse. Each pulse starts with an edge at its first sample,
// converted to tape time exactly (samples x 16 MHz / 13 / rate), so the same
// tape as a UEF gives the same edge times within one clock. A pulse longer than
// 100 ms is silence: it yields a gap, not an edge, so a detector that waits for
// silence sees silence. Z-RLE is inflated whole at open.
class CswTape final : public TapeSignal {
public:
    // Throws std::runtime_error with a sentence naming `name` if the image
    // cannot be read.
    static std::unique_ptr<CswTape> open(std::span<const uint8_t> bytes, const std::string& name);

    TapeItem next() override;
    void seek(TapeTime time) override;
    std::optional<TapeTime> duration() const override;
    TapeKind kind() const override { return TapeKind::Csw; }
    const TapeMetadata& metadata() const override { return metadata_; }

    uint32_t sample_rate() const { return sample_rate_; }

private:
    CswTape() = default;

    TapeTime time_of(uint64_t samples) const {
        return samples * ULA_CLOCK_NUMERATOR / (ULA_CLOCK_DENOMINATOR * sample_rate_);
    }

    // Every this many pulses the running sample count is kept, for seeking.
    static constexpr size_t CHECKPOINT_INTERVAL = 4096;

    TapeMetadata metadata_;
    uint32_t sample_rate_ = 0;
    bool initial_level_ = false;
    uint32_t gap_samples_ = 0;            // a pulse longer than this is silence
    std::vector<uint32_t> pulses_;
    std::vector<uint64_t> checkpoints_;   // samples before pulse k * CHECKPOINT_INTERVAL
    uint64_t total_samples_ = 0;

    size_t next_pulse_ = 0;
    uint64_t samples_before_next_ = 0;
};

}  // namespace beebium

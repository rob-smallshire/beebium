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

#ifndef BEEBIUM_LINUX_APP_LIVE_LEVEL_HPP
#define BEEBIUM_LINUX_APP_LIVE_LEVEL_HPP

#include "app/LiveInput.hpp"

#include <QByteArray>

#include <cstdint>

namespace beebium::linux_client {

// A captured chunk's level (client specification 9.0, Live input): the
// largest magnitude among the samples of the channel the deck takes, as a
// fraction of full scale, and whether any of them was at full scale, where
// the host's converter clips (16-bit -32768 or 32767, float 1.0 or more).
// Of two channels, the left, or the right when the stream takes the right.
struct ChunkLevel {
    float peak = 0.0F;
    bool clipped = false;
};
ChunkLevel chunk_level(const QByteArray& samples, const LiveInputFormat& format);

// What the meter draws from the chunks as they come: a peak that rises at
// once and falls DECAY_DB_PER_SECOND, between FLOOR_DB and full scale; and
// a clip mark that stays lit CLIP_HOLD_MS after the last clip.
class LevelMeterState {
public:
    static constexpr double FLOOR_DB = -60.0;
    static constexpr double DECAY_DB_PER_SECOND = 20.0;
    static constexpr std::int64_t CLIP_HOLD_MS = 1000;

    // A chunk's level, at a time in milliseconds.
    void take(const ChunkLevel& level, std::int64_t now_ms);
    double level_db() const { return level_db_; }
    // The bar's length, 0 at the floor to 1 at full scale.
    double fraction() const { return (level_db_ - FLOOR_DB) / -FLOOR_DB; }
    bool clip_lit(std::int64_t now_ms) const;
    void reset() { *this = {}; }

private:
    double level_db_ = FLOOR_DB;
    std::int64_t last_ms_ = 0;
    bool started_ = false;
    bool clipped_ = false;
    std::int64_t clip_ms_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_LIVE_LEVEL_HPP

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

// The meter's scale and timing (client specification 9.0, Live input;
// cassette round 12): decibels of full scale from the floor to 0, linear in
// dB across the bar, with a tick where a cassette signal should sit.
constexpr double METER_FLOOR_DB = -60.0;
constexpr double METER_TARGET_DB = -12.0;
constexpr double METER_BAR_FALL_DB_PER_S = 20.0;
constexpr double METER_HOLD_SECONDS = 2.0;
constexpr double METER_HOLD_FALL_DB_PER_S = 10.0;
// Clipping is a run of full-scale samples, not a single click at a splice
// or the head of a tape.
constexpr int CLIP_RUN_SAMPLES = 3;
constexpr double CLIP_HOLD_SECONDS = 2.0;

// A captured chunk's level: the largest magnitude among the samples of the
// channel the deck takes, as a fraction of full scale, and the longest run
// of consecutive samples there at full scale, where the host's converter
// clips (16-bit -32768 or 32767, float a magnitude of 1.0 or more). Of two
// channels, the left, or the right when the stream takes the right.
struct ChunkLevel {
    float peak = 0.0F;
    int clip_run = 0;
    bool clipping() const { return clip_run >= CLIP_RUN_SAMPLES; }
};
ChunkLevel chunk_level(const QByteArray& samples, const LiveInputFormat& format);

// A peak as decibels of full scale, at the floor for silence and anything
// beneath it.
double level_db(float peak);
// Where a level sits along the bar, 0 at the floor to 1 at full scale.
double meter_fraction(double db);

// What the meter draws from the chunks as they come, at times in
// milliseconds: the bar, rising at once and falling METER_BAR_FALL_DB_PER_S;
// the held peak, the highest level of the last METER_HOLD_SECONDS, then
// falling METER_HOLD_FALL_DB_PER_S until the bar overtakes it; and the clip
// mark, lit CLIP_HOLD_SECONDS after the last chunk that clipped.
class LevelMeterState {
public:
    void take(const ChunkLevel& level, std::int64_t now_ms);
    double bar_db(std::int64_t now_ms) const;
    double hold_db(std::int64_t now_ms) const;
    bool clip_lit(std::int64_t now_ms) const;
    void reset() { *this = {}; }

private:
    double bar_peak_db_ = METER_FLOOR_DB;
    std::int64_t bar_ms_ = 0;
    double hold_peak_db_ = METER_FLOOR_DB;
    std::int64_t hold_ms_ = 0;
    bool clipped_ = false;
    std::int64_t clip_ms_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_LIVE_LEVEL_HPP

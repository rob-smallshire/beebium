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

#include "app/LiveLevel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace beebium::linux_client {

ChunkLevel chunk_level(const QByteArray& samples, const LiveInputFormat& format,
                       int carried_run) {
    ChunkLevel level;
    level.end_run = carried_run;
    const int frame_bytes = format.bytes_per_frame();
    if (frame_bytes <= 0) {
        return level;
    }
    const bool s16 = format.sample == LiveInputFormat::Sample::s16;
    const int sample_bytes = s16 ? 2 : 4;
    const int channel = format.channels > 1 && format.channel == QStringLiteral("right") ? 1 : 0;
    const qsizetype frames = samples.size() / frame_bytes;
    const char* data = samples.constData();
    int run = carried_run;
    for (qsizetype i = 0; i < frames; ++i) {
        const char* at = data + i * frame_bytes + channel * sample_bytes;
        float magnitude = 0.0F;
        bool full = false;
        if (s16) {
            std::int16_t v = 0;
            std::memcpy(&v, at, sizeof v);
            full = v == INT16_MIN || v == INT16_MAX;
            magnitude = static_cast<float>(std::abs(static_cast<int>(v))) / 32768.0F;
        } else {
            float v = 0.0F;
            std::memcpy(&v, at, sizeof v);
            magnitude = std::fabs(v);
            full = magnitude >= 1.0F;
        }
        run = full ? run + 1 : 0;
        level.clip_run = std::max(level.clip_run, run);
        level.peak = std::max(level.peak, std::min(magnitude, 1.0F));
    }
    level.end_run = run;
    return level;
}

double level_db(float peak) {
    if (peak <= 0.0F) {
        return METER_FLOOR_DB;
    }
    return std::max(METER_FLOOR_DB, 20.0 * std::log10(static_cast<double>(peak)));
}

double meter_fraction(double db) {
    return std::clamp((db - METER_FLOOR_DB) / -METER_FLOOR_DB, 0.0, 1.0);
}

void LevelMeterState::take(const ChunkLevel& level, std::int64_t now_ms) {
    const double db = level_db(level.peak);
    if (db >= bar_db(now_ms)) {
        bar_peak_db_ = db;
        bar_ms_ = now_ms;
    }
    const double bar = bar_db(now_ms);
    if (bar >= hold_db(now_ms)) {
        hold_peak_db_ = bar;
        hold_ms_ = now_ms;
    }
    if (level.clipping()) {
        clipped_ = true;
        clip_ms_ = now_ms;
    }
}

double LevelMeterState::bar_db(std::int64_t now_ms) const {
    const double elapsed = std::max<double>(0.0, static_cast<double>(now_ms - bar_ms_) / 1000.0);
    return std::max(METER_FLOOR_DB, bar_peak_db_ - METER_BAR_FALL_DB_PER_S * elapsed);
}

double LevelMeterState::hold_db(std::int64_t now_ms) const {
    const double elapsed = static_cast<double>(now_ms - hold_ms_) / 1000.0 - METER_HOLD_SECONDS;
    if (elapsed <= 0.0) {
        return hold_peak_db_;
    }
    return std::max(METER_FLOOR_DB, hold_peak_db_ - METER_HOLD_FALL_DB_PER_S * elapsed);
}

bool LevelMeterState::clip_lit(std::int64_t now_ms) const {
    return clipped_ && static_cast<double>(now_ms - clip_ms_) < CLIP_HOLD_SECONDS * 1000.0;
}

}  // namespace beebium::linux_client

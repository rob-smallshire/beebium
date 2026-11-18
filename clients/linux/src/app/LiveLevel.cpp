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

ChunkLevel chunk_level(const QByteArray& samples, const LiveInputFormat& format) {
    ChunkLevel level;
    const int frame_bytes = format.bytes_per_frame();
    if (frame_bytes <= 0) {
        return level;
    }
    const bool s16 = format.sample == LiveInputFormat::Sample::s16;
    const int sample_bytes = s16 ? 2 : 4;
    const int channel = format.channels > 1 && format.channel == QStringLiteral("right") ? 1 : 0;
    const qsizetype frames = samples.size() / frame_bytes;
    const char* data = samples.constData();
    for (qsizetype i = 0; i < frames; ++i) {
        const char* at = data + i * frame_bytes + channel * sample_bytes;
        float magnitude = 0.0F;
        if (s16) {
            std::int16_t v = 0;
            std::memcpy(&v, at, sizeof v);
            level.clipped = level.clipped || v == INT16_MIN || v == INT16_MAX;
            magnitude = static_cast<float>(std::abs(static_cast<int>(v))) / 32768.0F;
        } else {
            float v = 0.0F;
            std::memcpy(&v, at, sizeof v);
            magnitude = std::fabs(v);
            level.clipped = level.clipped || magnitude >= 1.0F;
        }
        level.peak = std::max(level.peak, std::min(magnitude, 1.0F));
    }
    return level;
}

void LevelMeterState::take(const ChunkLevel& level, std::int64_t now_ms) {
    if (started_) {
        const double elapsed = static_cast<double>(now_ms - last_ms_) / 1000.0;
        level_db_ = std::max(FLOOR_DB, level_db_ - DECAY_DB_PER_SECOND * std::max(0.0, elapsed));
    }
    started_ = true;
    last_ms_ = now_ms;
    if (level.peak > 0.0F) {
        level_db_ = std::max(level_db_, std::max(FLOOR_DB, 20.0 * std::log10(level.peak)));
    }
    if (level.clipped) {
        clipped_ = true;
        clip_ms_ = now_ms;
    }
}

bool LevelMeterState::clip_lit(std::int64_t now_ms) const {
    return clipped_ && now_ms - clip_ms_ < CLIP_HOLD_MS;
}

}  // namespace beebium::linux_client

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

// A live input's level, from the client's own capture (client
// specification 9.0, Live input; cassette round 11).

#include "app/LiveLevel.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace beebium::linux_client;
using Catch::Approx;

namespace {

template <typename T>
QByteArray bytes(const std::vector<T>& samples) {
    QByteArray out(static_cast<qsizetype>(samples.size() * sizeof(T)), '\0');
    std::memcpy(out.data(), samples.data(), static_cast<size_t>(out.size()));
    return out;
}

LiveInputFormat format(LiveInputFormat::Sample sample, unsigned channels,
                       const QString& channel = QString()) {
    LiveInputFormat f;
    f.sample_rate = 48000;
    f.sample = sample;
    f.channels = channels;
    f.channel = channel;
    return f;
}

constexpr auto S16 = LiveInputFormat::Sample::s16;
constexpr auto F32 = LiveInputFormat::Sample::f32;

}  // namespace

TEST_CASE("A 16-bit mono chunk's peak is its largest magnitude; only full scale clips",
          "[live][level]") {
    const ChunkLevel quiet = chunk_level(bytes<std::int16_t>({100, -8192, 4096}), format(S16, 1));
    CHECK(quiet.peak == Approx(0.25));
    CHECK_FALSE(quiet.clipped);
    const ChunkLevel loud = chunk_level(bytes<std::int16_t>({0, 32766, -32767}), format(S16, 1));
    CHECK_FALSE(loud.clipped);
    CHECK(chunk_level(bytes<std::int16_t>({0, 32767}), format(S16, 1)).clipped);
    const ChunkLevel bottom = chunk_level(bytes<std::int16_t>({-32768, 0}), format(S16, 1));
    CHECK(bottom.clipped);
    CHECK(bottom.peak == Approx(1.0));
}

TEST_CASE("A float mono chunk clips at a magnitude of one or more", "[live][level]") {
    const ChunkLevel quiet = chunk_level(bytes<float>({0.1F, -0.5F, 0.25F}), format(F32, 1));
    CHECK(quiet.peak == Approx(0.5));
    CHECK_FALSE(quiet.clipped);
    CHECK_FALSE(chunk_level(bytes<float>({0.999F}), format(F32, 1)).clipped);
    CHECK(chunk_level(bytes<float>({-1.0F}), format(F32, 1)).clipped);
    const ChunkLevel over = chunk_level(bytes<float>({1.5F}), format(F32, 1));
    CHECK(over.clipped);
    CHECK(over.peak == Approx(1.0));  // drawn no further than full
}

TEST_CASE("A stereo chunk is measured on the left channel; a full right one lights nothing",
          "[live][level]") {
    // Frames of (left, right).
    const ChunkLevel s16 =
        chunk_level(bytes<std::int16_t>({1024, 32767, -2048, -32768}), format(S16, 2));
    CHECK(s16.peak == Approx(2048.0 / 32768.0));
    CHECK_FALSE(s16.clipped);
    const ChunkLevel f32 = chunk_level(bytes<float>({0.125F, 1.0F, -0.25F, -1.0F}), format(F32, 2));
    CHECK(f32.peak == Approx(0.25));
    CHECK_FALSE(f32.clipped);
    // As a stream's channel names it: left unless the deck takes the right.
    CHECK(chunk_level(bytes<float>({0.125F, 1.0F}), format(F32, 2, QStringLiteral("left"))).peak ==
          Approx(0.125));
    const ChunkLevel right = chunk_level(bytes<float>({0.125F, 1.0F}), format(F32, 2, QStringLiteral("right")));
    CHECK(right.peak == Approx(1.0));
    CHECK(right.clipped);
}

TEST_CASE("An empty chunk, or a part frame, is silence", "[live][level]") {
    CHECK(chunk_level(QByteArray(), format(S16, 1)).peak == 0.0F);
    // Three bytes of a 16-bit stereo frame: none whole.
    CHECK(chunk_level(QByteArray(3, '\x7f'), format(S16, 2)).peak == 0.0F);
}

TEST_CASE("The meter rises at once, falls about 20 dB a second, and holds a clip for a second",
          "[live][level]") {
    LevelMeterState meter;
    CHECK(meter.level_db() <= LevelMeterState::FLOOR_DB);
    meter.take({0.5F, false}, 0);
    CHECK(meter.level_db() == Approx(-6.02).margin(0.01));
    // A quieter chunk half a second later: the bar has fallen 10 dB.
    meter.take({0.01F, false}, 500);
    CHECK(meter.level_db() == Approx(-16.02).margin(0.01));
    // A louder one lifts it at once.
    meter.take({1.0F, true}, 600);
    CHECK(meter.level_db() == Approx(0.0).margin(0.01));
    CHECK(meter.clip_lit(600));
    CHECK(meter.clip_lit(1599));
    CHECK_FALSE(meter.clip_lit(1600));
    // Never below the floor, however long the silence.
    meter.take({0.0F, false}, 60000);
    CHECK(meter.level_db() == Approx(LevelMeterState::FLOOR_DB));
    CHECK(meter.fraction() == Approx(0.0));
    meter.take({1.0F, false}, 60001);
    CHECK(meter.fraction() == Approx(1.0));
}

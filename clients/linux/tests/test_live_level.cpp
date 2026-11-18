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

#include <cmath>
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

TEST_CASE("A 16-bit mono chunk's peak is its largest magnitude", "[live][level]") {
    const ChunkLevel quiet = chunk_level(bytes<std::int16_t>({100, -8192, 4096}), format(S16, 1));
    CHECK(quiet.peak == Approx(0.25));
    CHECK(quiet.clip_run == 0);
    const ChunkLevel bottom = chunk_level(bytes<std::int16_t>({-32768, 0}), format(S16, 1));
    CHECK(bottom.peak == Approx(1.0));
    CHECK(bottom.clip_run == 1);
}

TEST_CASE("A float mono chunk's peak is drawn no further than full scale", "[live][level]") {
    const ChunkLevel quiet = chunk_level(bytes<float>({0.1F, -0.5F, 0.25F}), format(F32, 1));
    CHECK(quiet.peak == Approx(0.5));
    CHECK(quiet.clip_run == 0);
    CHECK(chunk_level(bytes<float>({1.5F}), format(F32, 1)).peak == Approx(1.0));
}

TEST_CASE("Clipping is a run of three full-scale samples, not a single click", "[live][level]") {
    STATIC_CHECK(CLIP_RUN_SAMPLES == 3);
    // 16-bit, at either rail, and the rails mixed.
    const ChunkLevel click = chunk_level(bytes<std::int16_t>({0, 32767, 0, -32768, -32768, 0}), format(S16, 1));
    CHECK(click.clip_run == 2);
    CHECK_FALSE(click.clipping());
    CHECK(chunk_level(bytes<std::int16_t>({0, 32767, 32767, 32767, 0}), format(S16, 1)).clipping());
    CHECK(chunk_level(bytes<std::int16_t>({-32768, -32768, -32768}), format(S16, 1)).clipping());
    CHECK(chunk_level(bytes<std::int16_t>({32767, -32768, 32767, 1}), format(S16, 1)).clip_run == 3);
    // Not quite full scale is not clipping.
    CHECK(chunk_level(bytes<std::int16_t>({32766, 32766, 32766, -32767}), format(S16, 1)).clip_run == 0);
    // Float, at a magnitude of one or more.
    CHECK(chunk_level(bytes<float>({1.0F, -1.0F, 1.25F}), format(F32, 1)).clipping());
    CHECK(chunk_level(bytes<float>({0.999F, 0.999F, 0.999F}), format(F32, 1)).clip_run == 0);
    CHECK(chunk_level(bytes<float>({1.0F, 1.0F, 0.5F, 1.0F}), format(F32, 1)).clip_run == 2);
}

TEST_CASE("A stereo chunk is measured on the channel the deck takes; the other counts for nothing",
          "[live][level]") {
    // Frames of (left, right).
    const ChunkLevel s16 = chunk_level(
        bytes<std::int16_t>({1024, 32767, -2048, -32768, 0, 32767, 0, 32767}), format(S16, 2));
    CHECK(s16.peak == Approx(2048.0 / 32768.0));
    CHECK(s16.clip_run == 0);
    const ChunkLevel f32 =
        chunk_level(bytes<float>({0.125F, 1.0F, -0.25F, -1.0F, 0.0F, 1.0F}), format(F32, 2));
    CHECK(f32.peak == Approx(0.25));
    CHECK(f32.clip_run == 0);
    CHECK(chunk_level(bytes<float>({0.125F, 1.0F}), format(F32, 2, QStringLiteral("left"))).peak ==
          Approx(0.125));
    const ChunkLevel right = chunk_level(bytes<float>({0.125F, 1.0F, 0.0F, 1.0F, 0.5F, 1.0F}),
                                         format(F32, 2, QStringLiteral("right")));
    CHECK(right.peak == Approx(1.0));
    CHECK(right.clipping());
}

TEST_CASE("An empty chunk, or a part frame, is silence", "[live][level]") {
    CHECK(chunk_level(QByteArray(), format(S16, 1)).peak == 0.0F);
    // Three bytes of a 16-bit stereo frame: none whole.
    CHECK(chunk_level(QByteArray(3, '\x7f'), format(S16, 2)).peak == 0.0F);
}

TEST_CASE("The meter reads in decibels of full scale, linear across its width", "[live][level]") {
    STATIC_CHECK(METER_FLOOR_DB == -60.0);
    STATIC_CHECK(METER_TARGET_DB == -12.0);
    CHECK(level_db(1.0F) == Approx(0.0));
    CHECK(level_db(0.5F) == Approx(-6.02).margin(0.01));
    CHECK(level_db(0.251189F) == Approx(-12.0).margin(0.01));
    CHECK(level_db(0.001F) == Approx(-60.0));
    // Silence, and all below the floor, sit at the floor.
    CHECK(level_db(0.0F) == METER_FLOOR_DB);
    CHECK(level_db(0.00001F) == METER_FLOOR_DB);
    CHECK(meter_fraction(METER_FLOOR_DB) == Approx(0.0));
    CHECK(meter_fraction(METER_TARGET_DB) == Approx(0.8));
    CHECK(meter_fraction(-30.0) == Approx(0.5));
    CHECK(meter_fraction(0.0) == Approx(1.0));
    CHECK(meter_fraction(-90.0) == Approx(0.0));
}

namespace {

ChunkLevel at_db(double db) {
    return {static_cast<float>(std::pow(10.0, db / 20.0)), 0};
}

}  // namespace

TEST_CASE("The bar rises at once and falls 20 dB a second", "[live][level]") {
    STATIC_CHECK(METER_BAR_FALL_DB_PER_S == 20.0);
    LevelMeterState meter;
    CHECK(meter.bar_db(0) == METER_FLOOR_DB);
    meter.take(at_db(-6.0), 0);
    CHECK(meter.bar_db(0) == Approx(-6.0).margin(0.01));
    meter.take(at_db(-40.0), 500);
    CHECK(meter.bar_db(500) == Approx(-16.0).margin(0.01));
    CHECK(meter.bar_db(1000) == Approx(-26.0).margin(0.01));
    meter.take(at_db(-3.0), 1100);
    CHECK(meter.bar_db(1100) == Approx(-3.0).margin(0.01));
    // Never below the floor, however long the silence.
    meter.take({0.0F, 0}, 60000);
    CHECK(meter.bar_db(60000) == METER_FLOOR_DB);
}

TEST_CASE("The held peak sits two seconds, falls 10 dB a second, and a louder bar takes it",
          "[live][level]") {
    STATIC_CHECK(METER_HOLD_SECONDS == 2.0);
    STATIC_CHECK(METER_HOLD_FALL_DB_PER_S == 10.0);
    LevelMeterState meter;
    CHECK(meter.hold_db(0) == METER_FLOOR_DB);
    meter.take(at_db(-10.0), 0);
    // The bar falls away beneath it; the hold stays.
    for (std::int64_t t = 10; t <= 2000; t += 10) {
        meter.take(at_db(-50.0), t);
    }
    CHECK(meter.bar_db(2000) < -40.0);
    CHECK(meter.hold_db(1000) == Approx(-10.0).margin(0.01));
    CHECK(meter.hold_db(2000) == Approx(-10.0).margin(0.01));
    // Then it falls.
    CHECK(meter.hold_db(2500) == Approx(-15.0).margin(0.01));
    CHECK(meter.hold_db(3000) == Approx(-20.0).margin(0.01));
    // A louder bar overtakes it, and it is held anew there.
    meter.take(at_db(-18.0), 3000);
    CHECK(meter.hold_db(3000) == Approx(-18.0).margin(0.01));
    CHECK(meter.hold_db(4900) == Approx(-18.0).margin(0.01));
    // A quieter one within the two seconds does not move it.
    meter.take(at_db(-30.0), 4000);
    CHECK(meter.hold_db(4000) == Approx(-18.0).margin(0.01));
    // Never below the floor.
    CHECK(meter.hold_db(60000) == METER_FLOOR_DB);
}

TEST_CASE("The clip mark lights for sustained clipping, two seconds after the last, then is gone",
          "[live][level]") {
    STATIC_CHECK(CLIP_HOLD_SECONDS == 2.0);
    LevelMeterState meter;
    CHECK_FALSE(meter.clip_lit(0));
    meter.take({1.0F, 2}, 0);  // a click
    CHECK_FALSE(meter.clip_lit(0));
    meter.take({1.0F, 3}, 100);
    CHECK(meter.clip_lit(100));
    meter.take({1.0F, 40}, 500);
    CHECK(meter.clip_lit(2499));
    CHECK_FALSE(meter.clip_lit(2500));
    meter.reset();
    CHECK_FALSE(meter.clip_lit(600));
}

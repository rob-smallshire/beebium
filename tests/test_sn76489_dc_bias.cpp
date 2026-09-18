// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// DC bias metadata tests for SN76489 sound chip
//
// The SN76489 output is unipolar: it rests at a silence level (~0.8 V) and
// rises by a volume-dependent swing while the flip-flop is high, so the
// mid-point (mean) voltage varies with volume. This is what sampled-sound
// techniques exploit. trough = silence level, peak = silence + swing,
// dc_bias = silence + swing/2.
//
// Reference: scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "beebium/devices/Sn76489.hpp"

using namespace beebium;

TEST_CASE("SN76489 DC bias metadata", "[sn76489][dc_bias]") {

    SECTION("Silent channel has constant DC bias") {
        Sn76489 chip(4'000'000, 48'000);
        chip.write(0x9F);  // Volume = 15 (silent)

        auto state = chip.get_tone_channel_state(0);

        // Silent channel: dc_bias = peak = trough = DC_BIAS_SILENT_V (0.8V)
        REQUIRE_THAT(state.dc_bias_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE(state.peak_v == state.trough_v);  // No oscillation when silent
        REQUIRE_THAT(state.peak_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
    }

    SECTION("Max volume has widest swing") {
        Sn76489 chip(4'000'000, 48'000);
        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Volume = 0 (max)

        auto state = chip.get_tone_channel_state(0);

        // Max volume should have widest swing (0.8V peak-to-peak)
        REQUIRE(state.peak_v > state.trough_v);
        float swing = state.peak_v - state.trough_v;
        REQUIRE_THAT(swing, Catch::Matchers::WithinAbs(0.8f, 0.01f));

        // Trough is the silence level; mid-point = 0.8 + swing/2 = 1.2 V at max volume
        REQUIRE_THAT(state.trough_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE_THAT(state.dc_bias_v, Catch::Matchers::WithinAbs(1.2f, 0.01f));
    }

    SECTION("Swing decreases with increasing attenuation") {
        Sn76489 chip(4'000'000, 48'000);
        chip.write(0x85);  // Tone 0 freq = 5

        float prev_swing = 1.0f;  // Start larger than max
        for (uint8_t vol = 0; vol < 15; ++vol) {
            chip.write(0x90 | vol);
            auto state = chip.get_tone_channel_state(0);

            float swing = state.peak_v - state.trough_v;

            // Each step should have equal or smaller swing
            REQUIRE(swing <= prev_swing + 0.001f);  // Allow tiny tolerance

            // Trough is the constant silence level; the mid-point rises with the
            // swing (dc_bias = trough + swing/2).
            REQUIRE_THAT(state.trough_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
            REQUIRE_THAT(state.dc_bias_v,
                         Catch::Matchers::WithinAbs(0.8f + swing / 2.0f, 0.01f));

            prev_swing = swing;
        }

        // After volume 14, swing should be very small
        REQUIRE(prev_swing < 0.05f);
    }

    SECTION("Swing follows 2dB attenuation curve") {
        Sn76489 chip(4'000'000, 48'000);
        chip.write(0x85);  // Tone 0 freq = 5

        // Check that consecutive volumes differ by ~2dB (ratio ~0.794)
        constexpr float DB_RATIO = 0.794328235f;  // 10^(-2/20)

        for (uint8_t vol = 0; vol < 14; ++vol) {
            chip.write(0x90 | vol);
            auto state1 = chip.get_tone_channel_state(0);
            float swing1 = state1.peak_v - state1.trough_v;

            chip.write(0x90 | (vol + 1));
            auto state2 = chip.get_tone_channel_state(0);
            float swing2 = state2.peak_v - state2.trough_v;

            if (swing1 > 0.01f && swing2 > 0.01f) {
                float ratio = swing2 / swing1;
                // Allow 10% tolerance for integer rounding in the table
                REQUIRE(ratio > DB_RATIO * 0.9f);
                REQUIRE(ratio < DB_RATIO * 1.1f);
            }
        }
    }

    SECTION("Noise channel has same DC bias behavior") {
        Sn76489 chip(4'000'000, 48'000);

        // Test silent noise channel
        chip.write(0xE4);  // Noise: rate=0, white mode
        chip.write(0xFF);  // Volume = 15 (silent)

        auto silent_state = chip.get_noise_channel_state();
        REQUIRE_THAT(silent_state.dc_bias_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE(silent_state.peak_v == silent_state.trough_v);

        // Test max volume noise
        chip.write(0xF0);  // Volume = 0 (max)

        auto max_state = chip.get_noise_channel_state();
        REQUIRE_THAT(max_state.dc_bias_v, Catch::Matchers::WithinAbs(1.2f, 0.01f));
        float swing = max_state.peak_v - max_state.trough_v;
        REQUIRE_THAT(swing, Catch::Matchers::WithinAbs(0.8f, 0.01f));
    }

    SECTION("All tone channels have independent DC bias") {
        Sn76489 chip(4'000'000, 48'000);

        // Set different volumes for each channel
        chip.write(0x90);  // Tone 0 vol = 0 (max)
        chip.write(0xB5);  // Tone 1 vol = 5
        chip.write(0xDA);  // Tone 2 vol = 10

        auto state0 = chip.get_tone_channel_state(0);
        auto state1 = chip.get_tone_channel_state(1);
        auto state2 = chip.get_tone_channel_state(2);

        float swing0 = state0.peak_v - state0.trough_v;
        float swing1 = state1.peak_v - state1.trough_v;
        float swing2 = state2.peak_v - state2.trough_v;

        // All share the constant silence level (trough); the mid-point rises
        // with each channel's swing (dc_bias = 0.8 + swing/2).
        REQUIRE_THAT(state0.trough_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE_THAT(state1.trough_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE_THAT(state2.trough_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE_THAT(state0.dc_bias_v, Catch::Matchers::WithinAbs(0.8f + swing0 / 2.0f, 0.01f));
        REQUIRE_THAT(state1.dc_bias_v, Catch::Matchers::WithinAbs(0.8f + swing1 / 2.0f, 0.01f));
        REQUIRE_THAT(state2.dc_bias_v, Catch::Matchers::WithinAbs(0.8f + swing2 / 2.0f, 0.01f));

        // But different swings based on volume
        REQUIRE(swing0 > swing1);
        REQUIRE(swing1 > swing2);
    }
}

TEST_CASE("SN76489 DC bias voltage bounds", "[sn76489][dc_bias]") {

    SECTION("Peak and trough are symmetric around DC bias") {
        Sn76489 chip(4'000'000, 48'000);
        chip.write(0x85);  // Tone 0 freq = 5

        for (uint8_t vol = 0; vol < 15; ++vol) {
            chip.write(0x90 | vol);
            auto state = chip.get_tone_channel_state(0);

            // Peak and trough should be equidistant from DC bias
            float peak_offset = state.peak_v - state.dc_bias_v;
            float trough_offset = state.dc_bias_v - state.trough_v;

            REQUIRE_THAT(peak_offset, Catch::Matchers::WithinAbs(trough_offset, 0.001f));
        }
    }

    SECTION("Trough voltage is never negative") {
        Sn76489 chip(4'000'000, 48'000);
        chip.write(0x85);  // Tone 0 freq = 5

        for (uint8_t vol = 0; vol <= 15; ++vol) {
            chip.write(0x90 | vol);
            auto state = chip.get_tone_channel_state(0);

            // Trough is the silence level at every volume; peak rises to
            // 0.8 + swing, at most 1.6 V at max volume.
            REQUIRE_THAT(state.trough_v, Catch::Matchers::WithinAbs(0.8f, 0.01f));
            REQUIRE(state.peak_v <= 1.61f);
        }
    }
}

TEST_CASE("SN76489 compute_voltage_levels static method", "[sn76489][dc_bias]") {

    SECTION("Direct computation matches state query") {
        float dc_bias, peak, trough;

        // Test volume 0 (max): mid-point = 0.8 + 0.8/2 = 1.2 V
        Sn76489::compute_voltage_levels(0, dc_bias, peak, trough);
        REQUIRE_THAT(dc_bias, Catch::Matchers::WithinAbs(1.2f, 0.01f));
        REQUIRE_THAT(peak - trough, Catch::Matchers::WithinAbs(0.8f, 0.01f));
        REQUIRE_THAT(trough, Catch::Matchers::WithinAbs(0.8f, 0.01f));  // silence level

        // Test volume 15 (silent)
        Sn76489::compute_voltage_levels(15, dc_bias, peak, trough);
        REQUIRE(peak == trough);
        REQUIRE_THAT(dc_bias, Catch::Matchers::WithinAbs(0.8f, 0.01f));

        // Test mid volume (5): mid-point = 0.8 + swing/2, swing ~0.253 -> ~0.927 V
        Sn76489::compute_voltage_levels(5, dc_bias, peak, trough);
        REQUIRE_THAT(dc_bias, Catch::Matchers::WithinAbs(0.927f, 0.01f));
        REQUIRE(peak > trough);
        REQUIRE(peak - trough < 0.8f);  // Less than max swing
    }
}

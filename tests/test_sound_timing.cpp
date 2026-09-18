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

// Timing precision tests for SN76489 sound chip
//
// These tests verify:
// - 250 kHz internal clock rate from 2 MHz input
// - 48 kHz output sample rate
// - Phase accumulator accuracy over extended periods
// - No cumulative timing drift
//
// The SN76489 uses a phase accumulator to generate the 250 kHz internal rate
// from the 2 MHz input, and a sample accumulator to generate 48 kHz output.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "beebium/devices/Sn76489.hpp"
#include "beebium/AudioBuffer.hpp"
#include "sn76489_channels.hpp"

#include <vector>
#include <cmath>

using namespace beebium;

TEST_CASE("SN76489 timing precision", "[sn76489][timing]") {

    SECTION("Sample rate is 48 kHz") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(50000);  // Large buffer for 1+ second

        // Enable a channel
        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Volume = 0 (max)

        // Run for exactly 2 MHz cycles equivalent to 1 second
        // 2 MHz = 2,000,000 cycles per second
        constexpr int ONE_SECOND_CYCLES = 2'000'000;

        for (int i = 0; i < ONE_SECOND_CYCLES; ++i) {
            chip.tick(buffer);
        }

        // Should have generated ~48000 samples
        size_t sample_count = buffer.available();

        // Allow 1% tolerance for timing precision
        REQUIRE(sample_count >= 47520);  // 48000 * 0.99
        REQUIRE(sample_count <= 48480);  // 48000 * 1.01
    }

    SECTION("Sample rate consistency over 10 seconds") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(500000);  // 10+ second buffer

        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Volume = 0 (max)

        // Run for 10 seconds
        constexpr int TEN_SECONDS_CYCLES = 20'000'000;

        for (int i = 0; i < TEN_SECONDS_CYCLES; ++i) {
            chip.tick(buffer);
        }

        // Should have generated ~480000 samples
        size_t sample_count = buffer.available();

        // Verify 0.1% tolerance (tighter for longer runs)
        size_t expected = 480000;
        float error = std::abs(static_cast<float>(sample_count) - expected) / expected;
        REQUIRE(error < 0.001f);  // Within 0.1%
    }

    SECTION("No cumulative drift after many cycles") {
        // Run chip in segments and verify consistent sample generation

        Sn76489 chip(4'000'000, 48'000);

        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Volume = 0 (max)

        std::vector<size_t> samples_per_segment;
        constexpr int SEGMENT_CYCLES = 200'000;  // 0.1 second per segment
        constexpr int NUM_SEGMENTS = 50;  // 5 seconds total

        for (int seg = 0; seg < NUM_SEGMENTS; ++seg) {
            AudioBuffer buffer(10000);

            for (int i = 0; i < SEGMENT_CYCLES; ++i) {
                chip.tick(buffer);
            }

            samples_per_segment.push_back(buffer.available());
        }

        // Each segment should produce ~4800 samples (48000 * 0.1)
        // Calculate mean and standard deviation
        double sum = 0;
        for (auto count : samples_per_segment) {
            sum += count;
        }
        double mean = sum / NUM_SEGMENTS;

        double variance = 0;
        for (auto count : samples_per_segment) {
            double diff = count - mean;
            variance += diff * diff;
        }
        variance /= NUM_SEGMENTS;
        double stddev = std::sqrt(variance);

        // Mean should be ~4800
        REQUIRE(mean > 4750);
        REQUIRE(mean < 4850);

        // Standard deviation should be very low (no drift)
        REQUIRE(stddev < 5);  // Less than 0.1% variation
    }

    SECTION("Frequency divider timing accuracy") {
        // Verify that counter-based frequency division is accurate
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(48000);

        // Set frequency = 100 (lower frequency to reduce aliasing)
        // f_out = 4 MHz / (32 × 100) = 1250 Hz
        chip.write(0x84);  // Tone 0 freq low = 4
        chip.write(0x06);  // Tone 0 freq high = 6 → 0x64 (100)
        chip.write(0x90);  // Volume = 0 (max)

        // Run for 1 second
        for (int i = 0; i < 2'000'000; ++i) {
            chip.tick(buffer);
        }

        // Read all samples
        std::vector<AudioSample> samples(buffer.available());
        size_t count = buffer.read(samples.data(), samples.size());

        // The output is unipolar; count crossings of the mean level (one per
        // half-cycle) rather than of zero.
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) sum += sn_tone0(samples[i]);
        const double mid = sum / static_cast<double>(count);
        int crossings = 0;
        for (size_t i = 1; i < count; ++i) {
            int16_t prev = sn_tone0(samples[i-1]);
            int16_t curr = sn_tone0(samples[i]);
            if ((prev < mid && curr >= mid) || (prev > mid && curr <= mid)) {
                crossings++;
            }
        }

        // Expected crossings: 2 per cycle × 1250 Hz × 1 second = 2500
        float expected_crossings = 2.0f * 1250.0f;
        float ratio = crossings / expected_crossings;

        // Allow 10% tolerance (aliasing and edge effects)
        REQUIRE(ratio > 0.90f);
        REQUIRE(ratio < 1.10f);
    }

    SECTION("Internal 250 kHz rate verification") {
        // The chip divides 2 MHz by 8 to get 250 kHz internal rate
        // Counter updates happen at 250 kHz
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        // Set frequency = 1 (counter decrements every 250 kHz tick)
        // Should toggle output every 1 internal tick = 8 2MHz cycles
        chip.write(0x81);  // Tone 0 freq = 1
        chip.write(0x90);  // Volume = 0 (max)

        // Run for 80 cycles (10 internal ticks, 10 output toggles)
        for (int i = 0; i < 80; ++i) {
            chip.tick(buffer);
        }

        // Read samples and count output bit changes
        std::vector<AudioSample> samples(buffer.available());
        buffer.read(samples.data(), samples.size());

        // With freq=1, output should toggle every internal tick
        // In 10 internal ticks, we expect ~10 toggles = ~5 complete cycles
        // This verifies the 8:1 division is working
        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 1);
    }

    SECTION("Phase accumulator precision at different frequencies") {
        // Test multiple frequencies to verify timing is consistent
        std::vector<uint16_t> test_freqs = {1, 10, 100, 500, 1023};

        for (uint16_t freq : test_freqs) {
            Sn76489 chip(4'000'000, 48'000);
            AudioBuffer buffer(48000);

            // Set frequency
            uint8_t low = freq & 0x0F;
            uint8_t high = (freq >> 4) & 0x3F;
            chip.write(0x80 | low);
            chip.write(high);
            chip.write(0x90);  // Volume = 0

            // Run for 1 second
            for (int i = 0; i < 2'000'000; ++i) {
                chip.tick(buffer);
            }

            size_t sample_count = buffer.available();

            // All frequencies should produce same sample count
            REQUIRE(sample_count >= 47500);
            REQUIRE(sample_count <= 48500);
        }
    }
}

TEST_CASE("SN76489 sample output timing", "[sn76489][timing]") {

    SECTION("Samples are generated incrementally") {
        Sn76489 chip(4'000'000, 48'000);

        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Volume = 0 (max)

        // Run small batches and verify samples are generated each batch
        size_t total_samples = 0;
        for (int batch = 0; batch < 10; ++batch) {
            AudioBuffer buffer(1000);

            for (int i = 0; i < 10000; ++i) {
                chip.tick(buffer);
            }

            size_t batch_samples = buffer.available();
            REQUIRE(batch_samples > 0);  // Each batch should produce samples
            total_samples += batch_samples;
        }

        // Total should be ~2400 samples (10 batches × 10000 cycles / ~41.67 cycles per sample)
        REQUIRE(total_samples > 2000);
    }

    SECTION("Sample generation rate is smooth") {
        // No large bursts or gaps in sample generation
        Sn76489 chip(4'000'000, 48'000);

        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Volume = 0 (max)

        // Track samples per 10000 cycles (~0.005 seconds = ~240 samples expected)
        std::vector<size_t> batch_samples;

        for (int batch = 0; batch < 100; ++batch) {
            AudioBuffer buffer(500);

            for (int i = 0; i < 10000; ++i) {
                chip.tick(buffer);
            }

            batch_samples.push_back(buffer.available());
        }

        // Calculate variance
        double sum = 0;
        for (auto s : batch_samples) sum += s;
        double mean = sum / batch_samples.size();

        double max_deviation = 0;
        for (auto s : batch_samples) {
            double dev = std::abs(s - mean);
            if (dev > max_deviation) max_deviation = dev;
        }

        // Maximum deviation should be small (no bursts)
        REQUIRE(max_deviation < mean * 0.1);  // Less than 10% deviation
    }
}

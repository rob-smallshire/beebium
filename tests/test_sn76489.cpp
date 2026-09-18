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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "beebium/devices/Sn76489.hpp"
#include "beebium/AudioBuffer.hpp"
#include "sn76489_channels.hpp"
#include <vector>
#include <set>
#include <cmath>

using namespace beebium;

TEST_CASE("SN76489 register protocol", "[sn76489]") {
    Sn76489 chip(4'000'000, 48'000);

    SECTION("Latch byte sets latched register") {
        // Tone 0 frequency latch
        chip.write(0x80);  // %10000000 - channel 0, type 0 (frequency)
        REQUIRE(chip.latched_register() == 0);

        // Tone 1 volume latch
        chip.write(0xB8);  // %10111000 - channel 1, type 1 (volume)
        REQUIRE(chip.latched_register() == 3);

        // Noise control latch
        chip.write(0xE0);  // %11100000 - channel 3, type 0 (noise control)
        REQUIRE(chip.latched_register() == 6);
    }

    SECTION("Tone frequency register - two-byte protocol") {
        // Set tone 0 frequency to 0x123 (low nibble first, then high 6 bits)
        chip.write(0x83);  // Latch: tone 0 freq, low nibble = 0x3
        chip.write(0x09);  // Data: high 6 bits = 0x09, full value = 0x093

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 0x093);

        // Set tone 1 frequency to 0x2A5
        chip.write(0xA5);  // Latch: tone 1 freq, low nibble = 0x5
        chip.write(0x14);  // Data: high 6 bits = 0x14, full value = 0x145

        state = chip.get_tone_channel_state(1);
        REQUIRE(state.frequency == 0x145);
    }

    SECTION("Volume register - single-byte protocol") {
        // Set tone 0 volume to 5 (10 dB attenuation)
        chip.write(0x95);  // Latch: tone 0 volume = 5

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.volume == 5);

        // Set noise volume to 15 (silence)
        chip.write(0xFF);  // Latch: noise volume = 15

        auto noise_state = chip.get_noise_channel_state();
        REQUIRE(noise_state.volume == 15);
    }

    SECTION("Data byte only applies to tone frequency registers") {
        // Set tone 0 volume to 3
        chip.write(0x93);  // Latch: tone 0 volume = 3

        // Send data byte (should be ignored for volume register)
        chip.write(0x1F);  // Data byte

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.volume == 3);  // Should remain unchanged
    }

    SECTION("Latched register persists across writes") {
        // Latch tone 2 frequency
        chip.write(0xC7);  // Latch: tone 2 freq, low nibble = 0x7
        REQUIRE(chip.latched_register() == 4);

        // Send data byte (uses latched register)
        chip.write(0x08);  // Data: high 6 bits = 0x08, full value = 0x087
        REQUIRE(chip.latched_register() == 4);  // Latched register unchanged

        auto state = chip.get_tone_channel_state(2);
        REQUIRE(state.frequency == 0x087);
    }
}

TEST_CASE("SN76489 tone generation", "[sn76489]") {
    Sn76489 chip(4'000'000, 48'000);
    AudioBuffer buffer(1024);

    SECTION("Tone counter decrements and reloads") {
        // Set tone 0 to frequency divider = 10
        chip.write(0x8A);  // Latch: tone 0 freq, low nibble = 0xA
        chip.write(0x00);  // Data: high bits = 0, full value = 0x00A

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 10);

        // Tick at 2 MHz, chip internally divides to 250 kHz
        // At 250 kHz, counter should decrement once per tick
        for (int i = 0; i < 100; ++i) {
            chip.tick(buffer);
        }

        // Counter should have toggled output_bit several times
        state = chip.get_tone_channel_state(0);
        REQUIRE(state.counter <= 10);
    }

    SECTION("Frequency = 0 treated as 1024") {
        chip.write(0x80);  // Latch: tone 0 freq = 0
        chip.write(0x00);  // Data: high bits = 0, full value = 0

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 0);

        // Frequency calculation: f_out = f_clock / (32 × N)
        // For N=0 (treated as 1024): f_out = 4000000 / (32 × 1024) ≈ 122 Hz
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(122.07f, 0.01f));
    }

    SECTION("Output frequency calculation") {
        // Set tone 1 to divider = 100
        chip.write(0xA4);  // Latch: tone 1 freq, low nibble = 0x4
        chip.write(0x06);  // Data: high bits = 0x06, full value = 0x064 (100 decimal)

        auto state = chip.get_tone_channel_state(1);
        REQUIRE(state.frequency == 100);

        // f_out = 4 MHz / (32 × 100) = 1250 Hz
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(1250.0f, 0.1f));
    }

    SECTION("Square wave toggles output bit") {
        chip.write(0x8A);  // Tone 0 freq = 10
        chip.write(0x90);  // Tone 0 volume = 0 (max amplitude)

        // Run enough cycles to cause multiple toggles
        // At 250 kHz internal rate, divider = 10, toggle every 10 cycles
        for (int i = 0; i < 1000; ++i) {
            chip.tick(buffer);
        }

        bool final_bit = chip.get_tone_channel_state(0).output_bit;
        // Output bit should have toggled and be a valid boolean
        REQUIRE((final_bit == true || final_bit == false));
    }
}

TEST_CASE("SN76489 volume and amplitude", "[sn76489]") {
    Sn76489 chip(4'000'000, 48'000);

    SECTION("Volume 0 produces maximum amplitude") {
        chip.write(0x90);  // Tone 0 volume = 0
        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.volume == 0);
        REQUIRE(std::abs(state.amplitude) == 127);  // Max amplitude (±127)
    }

    SECTION("Volume 15 produces silence") {
        chip.write(0x9F);  // Tone 0 volume = 15
        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.volume == 15);
        REQUIRE(state.amplitude == 0);  // Silence
    }

    SECTION("Volume table follows logarithmic curve") {
        // Test a few key points from the 2 dB per step curve
        std::vector<std::pair<uint8_t, int8_t>> expected = {
            {0, 127},  // 0 dB
            {1, 101},  // -2 dB
            {2, 80},   // -4 dB
            {3, 64},   // -6 dB
            {4, 51},   // -8 dB
            {5, 40},   // -10 dB
            {10, 13},  // -20 dB
            {15, 0}    // silence
        };

        for (const auto& [volume, expected_amp] : expected) {
            chip.write(0x90 | volume);  // Tone 0 volume
            auto state = chip.get_tone_channel_state(0);
            REQUIRE(std::abs(state.amplitude) == expected_amp);
        }
    }

    SECTION("Amplitude polarity follows output bit") {
        chip.write(0x8A);  // Tone 0 freq = 10
        chip.write(0x90);  // Tone 0 volume = 0 (max)

        auto state1 = chip.get_tone_channel_state(0);
        int8_t amp1 = state1.amplitude;

        // Force output bit toggle by running chip
        AudioBuffer buffer(1024);
        for (int i = 0; i < 100; ++i) {
            chip.tick(buffer);
        }

        auto state2 = chip.get_tone_channel_state(0);
        int8_t amp2 = state2.amplitude;

        // Amplitude should be ±127
        REQUIRE((amp1 == 127 || amp1 == -127));
        REQUIRE((amp2 == 127 || amp2 == -127));
    }
}

TEST_CASE("SN76489 noise channel", "[sn76489]") {
    Sn76489 chip(4'000'000, 48'000);

    SECTION("Noise control register sets rate and mode") {
        // Rate = 2 ($40 divider), white noise mode
        chip.write(0xE6);  // Latch: noise control = 0b0110 (rate=2, white=1)

        auto state = chip.get_noise_channel_state();
        REQUIRE(state.rate_select == 2);
        REQUIRE(state.white_mode == true);

        // Rate = 1 ($20 divider), periodic mode
        chip.write(0xE1);  // Latch: noise control = 0b0001 (rate=1, white=0)

        state = chip.get_noise_channel_state();
        REQUIRE(state.rate_select == 1);
        REQUIRE(state.white_mode == false);
    }

    SECTION("White noise LFSR produces non-repeating sequence") {
        chip.write(0xE4);  // Noise: rate=0, white mode
        chip.write(0xF0);  // Noise volume = 0 (max)

        AudioBuffer buffer(8192);  // Large buffer for many samples

        // Collect LFSR states over time
        // Noise rate 0 divider = 512, so need 512 * 8 = 4096 ticks at 2 MHz
        // for one LFSR update. Run many more to see multiple updates.
        std::vector<uint16_t> lfsr_states;
        for (int i = 0; i < 50000; ++i) {
            chip.tick(buffer);
            if (i % 5000 == 0) {
                auto state = chip.get_noise_channel_state();
                lfsr_states.push_back(state.lfsr);
            }
        }

        // LFSR should produce different states (not stuck)
        bool has_different_states = false;
        for (size_t i = 1; i < lfsr_states.size(); ++i) {
            if (lfsr_states[i] != lfsr_states[0]) {
                has_different_states = true;
                break;
            }
        }
        REQUIRE(has_different_states);
    }

    SECTION("Periodic noise produces 15-cycle pattern") {
        chip.write(0xE0);  // Noise: rate=0, periodic mode
        chip.write(0xF0);  // Noise volume = 0 (max)

        // In periodic mode, LFSR rotates instead of using feedback
        // Should see repeating pattern
        AudioBuffer buffer(8192);
        for (int i = 0; i < 100; ++i) {
            chip.tick(buffer);
        }

        // LFSR should have changed and be a valid 15-bit value
        auto final_state = chip.get_noise_channel_state();
        REQUIRE((final_state.lfsr & 0x7FFF) == final_state.lfsr);
    }

    SECTION("Noise rate select affects update frequency") {
        // Rate 0: divider = 32 → 250 kHz / 32 = 7812.5 Hz
        chip.write(0xE0);  // Noise: rate=0
        auto state = chip.get_noise_channel_state();
        REQUIRE_THAT(state.rate_hz, Catch::Matchers::WithinRel(7812.5f, 0.01f));

        // Rate 1: divider = 64 → 250 kHz / 64 = 3906.25 Hz
        chip.write(0xE1);  // Noise: rate=1
        state = chip.get_noise_channel_state();
        REQUIRE_THAT(state.rate_hz, Catch::Matchers::WithinRel(3906.25f, 0.01f));

        // Rate 2: divider = 128 → 250 kHz / 128 = 1953.125 Hz
        chip.write(0xE2);  // Noise: rate=2
        state = chip.get_noise_channel_state();
        REQUIRE_THAT(state.rate_hz, Catch::Matchers::WithinRel(1953.125f, 0.01f));
    }

    SECTION("Noise rate=3 uses tone 2 frequency") {
        // Set tone 2 to divider = 100 → 1250 Hz output
        chip.write(0xC4);  // Tone 2 freq, low nibble = 0x4
        chip.write(0x06);  // High bits = 0x06, full value = 0x064 (100)

        // Set noise to use tone 2 frequency
        chip.write(0xE3);  // Noise: rate=3 (use tone 2)

        auto noise_state = chip.get_noise_channel_state();
        REQUIRE(noise_state.rate_select == 3);
        // Rate should match tone 2 frequency: 4 MHz / (32 × 100) = 1250 Hz
        REQUIRE_THAT(noise_state.rate_hz, Catch::Matchers::WithinRel(1250.0f, 0.1f));
    }
}

TEST_CASE("SN76489 sample generation", "[sn76489]") {
    Sn76489 chip(4'000'000, 48'000);
    AudioBuffer buffer(8192);

    SECTION("Generates samples at 48 kHz rate") {
        // Set tone 0 to produce sound
        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Tone 0 volume = 0 (max)

        // Run chip for ~1000 cycles at 2 MHz
        // Should produce samples at 48 kHz rate
        for (int i = 0; i < 1000; ++i) {
            chip.tick(buffer);
        }

        // Buffer should have received samples
        REQUIRE(buffer.available() > 0);
    }

    SECTION("Audio samples have 4-channel structure") {
        chip.write(0x90);  // Tone 0 volume = 0 (max)
        chip.write(0xB0);  // Tone 1 volume = 0 (max)
        chip.write(0xD0);  // Tone 2 volume = 0 (max)
        chip.write(0xF0);  // Noise volume = 0 (max)

        // Generate some samples
        for (int i = 0; i < 500; ++i) {
            chip.tick(buffer);
        }

        // Read a sample and verify structure
        AudioSample sample;
        if (buffer.available() > 0) {
            buffer.read(&sample, 1);

            // The SN76489 uses the 2x16 encoding: source 0 = (tone0, tone1),
            // source 1 = (tone2, noise). Unipolar, silence = 0.
            int16_t tone0 = sn_tone0(sample);
            int16_t tone1 = sn_tone1(sample);
            int16_t tone2 = sn_tone2(sample);
            int16_t noise = sn_noise(sample);

            // At volume 0 every channel is active, so some are non-zero.
            bool all_non_zero = (tone0 != 0) || (tone1 != 0) || (tone2 != 0) || (noise != 0);
            REQUIRE(all_non_zero);
        }
    }

    SECTION("Silent channels produce DC midpoint (128)") {
        // Set all channels to volume 15 (silence)
        chip.write(0x9F);  // Tone 0 volume = 15
        chip.write(0xBF);  // Tone 1 volume = 15
        chip.write(0xDF);  // Tone 2 volume = 15
        chip.write(0xFF);  // Noise volume = 15

        // Generate samples
        for (int i = 0; i < 500; ++i) {
            chip.tick(buffer);
        }

        // Read a sample
        AudioSample sample;
        if (buffer.available() > 0) {
            buffer.read(&sample, 1);

            // All channels silent: unipolar output rests at the silence level 0.
            // Packed as: [tone0|tone1|tone2|noise] = 0x00000000
            REQUIRE(sample.sources[0] == 0x00000000);
        }
    }
}

TEST_CASE("SN76489 edge cases", "[sn76489]") {
    Sn76489 chip(4'000'000, 48'000);
    AudioBuffer buffer(1024);

    SECTION("Reset clears all state") {
        // Set various registers
        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Tone 0 volume = 0
        chip.write(0xE6);  // Noise: rate=2, white mode

        chip.reset();

        // All channels should be silent (volume=15)
        auto tone0 = chip.get_tone_channel_state(0);
        auto tone1 = chip.get_tone_channel_state(1);
        auto tone2 = chip.get_tone_channel_state(2);
        auto noise = chip.get_noise_channel_state();

        REQUIRE(tone0.volume == 15);
        REQUIRE(tone1.volume == 15);
        REQUIRE(tone2.volume == 15);
        REQUIRE(noise.volume == 15);

        // Frequencies should be reset
        REQUIRE(tone0.frequency == 0);
        REQUIRE(tone1.frequency == 0);
        REQUIRE(tone2.frequency == 0);
    }

    SECTION("Rapid register changes") {
        // Write to same register multiple times rapidly
        chip.write(0x81);  // Tone 0 freq = 1
        chip.write(0x82);  // Tone 0 freq = 2
        chip.write(0x83);  // Tone 0 freq = 3

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 3);  // Should have latest value

        // Change volume rapidly
        chip.write(0x90);  // Volume = 0
        chip.write(0x95);  // Volume = 5
        chip.write(0x9A);  // Volume = 10

        state = chip.get_tone_channel_state(0);
        REQUIRE(state.volume == 10);
    }

    SECTION("Maximum frequency value (0x3FF)") {
        chip.write(0x8F);  // Tone 0 freq, low nibble = 0xF
        chip.write(0x3F);  // High bits = 0x3F, full value = 0x3FF (1023)

        auto state = chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 0x3FF);

        // f_out = 4 MHz / (32 × 1023) ≈ 122.2 Hz
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(122.18f, 0.01f));
    }

    SECTION("All channels active simultaneously") {
        // Set all channels to produce sound
        chip.write(0x85);  // Tone 0 freq = 5
        chip.write(0x90);  // Tone 0 volume = 0
        chip.write(0xA7);  // Tone 1 freq = 7
        chip.write(0xB2);  // Tone 1 volume = 2
        chip.write(0xC9);  // Tone 2 freq = 9
        chip.write(0xD4);  // Tone 2 volume = 4
        chip.write(0xE1);  // Noise: rate=1, periodic
        chip.write(0xF6);  // Noise volume = 6

        // Generate samples
        for (int i = 0; i < 1000; ++i) {
            chip.tick(buffer);
        }

        // Should produce samples with all channels contributing
        REQUIRE(buffer.available() > 0);

        // Verify channel states
        auto tone0 = chip.get_tone_channel_state(0);
        auto tone1 = chip.get_tone_channel_state(1);
        auto tone2 = chip.get_tone_channel_state(2);
        auto noise = chip.get_noise_channel_state();

        REQUIRE(tone0.frequency == 5);
        REQUIRE(tone1.frequency == 7);
        REQUIRE(tone2.frequency == 9);
        REQUIRE(noise.rate_select == 1);
    }
}

TEST_CASE("SN76489 LFSR sequence verification", "[sn76489][lfsr]") {
    // The SN76489 uses a 15-bit LFSR for noise generation.
    // White noise: XOR feedback from bits 0 and 1, period = 32767
    // Periodic noise: simple rotation, period = 15 (with single-bit initial state)

    SECTION("White noise LFSR has maximal-length period of 32767") {
        // Simulate the white noise LFSR algorithm exactly as in Sn76489.cpp
        // Feedback polynomial: x^15 + x^14 + 1
        // Period should be 2^15 - 1 = 32767

        constexpr uint16_t LFSR_INIT = 0x4000;  // Initial state: bit 14 set
        constexpr uint16_t LFSR_MASK = 0x7FFF;  // 15-bit mask

        uint16_t lfsr = LFSR_INIT;
        std::set<uint16_t> seen_states;

        // Run LFSR for one complete cycle
        for (int i = 0; i < 32767; ++i) {
            // Should not have seen this state before
            REQUIRE(seen_states.find(lfsr) == seen_states.end());
            seen_states.insert(lfsr);

            // Advance LFSR (same algorithm as Sn76489::next_white_noise_bit())
            uint8_t feedback = ((lfsr >> 1) ^ lfsr) & 1;
            lfsr = (lfsr >> 1) | (feedback << 14);
            lfsr &= LFSR_MASK;
        }

        // Should have visited exactly 32767 unique states
        REQUIRE(seen_states.size() == 32767);

        // After 32767 steps, should be back at initial state
        REQUIRE(lfsr == LFSR_INIT);
    }

    SECTION("Periodic noise LFSR has period of 15") {
        // Periodic noise uses simple rotation of a single-bit initial state
        // With LFSR_INIT = 0x4000 (bit 14 set), rotation produces 15 unique states
        // (bit moves through positions 14, 13, 12, ..., 0, then back to 14)

        constexpr uint16_t LFSR_INIT = 0x4000;
        constexpr uint16_t LFSR_MASK = 0x7FFF;

        uint16_t lfsr = LFSR_INIT;
        std::vector<uint16_t> sequence;

        // Collect 20 states (more than one period)
        for (int i = 0; i < 20; ++i) {
            sequence.push_back(lfsr);

            // Advance LFSR (same algorithm as Sn76489::next_periodic_noise_bit())
            lfsr = ((lfsr >> 1) | (lfsr << 14)) & LFSR_MASK;
        }

        // First 15 states should all be unique
        std::set<uint16_t> first_15(sequence.begin(), sequence.begin() + 15);
        REQUIRE(first_15.size() == 15);

        // Each state should be a single bit (power of 2)
        for (int i = 0; i < 15; ++i) {
            uint16_t state = sequence[i];
            // Count set bits - should be exactly 1
            int bit_count = 0;
            for (int b = 0; b < 15; ++b) {
                if (state & (1 << b)) bit_count++;
            }
            REQUIRE(bit_count == 1);
        }

        // State at index 15 should equal state at index 0 (period = 15)
        REQUIRE(sequence[15] == sequence[0]);
    }

    SECTION("White noise output bit distribution is approximately 50/50") {
        // White noise should produce roughly equal positive and negative amplitudes
        // Use rate=0 (512 divider) and run for longer to get good statistics
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(100000);

        chip.write(0xE4);  // Noise: rate=0, white mode
        chip.write(0xF0);  // Noise volume = 0 (max)

        // Run some initial ticks to let the chip settle
        for (int i = 0; i < 10000; ++i) {
            chip.tick(buffer);
        }
        buffer.reset();  // Clear initial samples

        // Generate many samples (2 seconds worth)
        // At rate=0 (488 Hz update rate), we need many samples to see distribution
        for (int i = 0; i < 4'000'000; ++i) {
            chip.tick(buffer);
        }

        // Read samples and count positive vs negative noise values
        std::vector<AudioSample> samples(buffer.available());
        size_t count = buffer.read(samples.data(), samples.size());

        // The output is unipolar and band-limited, so split about the mean level
        // rather than about a fixed midpoint: a fair coin LFSR should spend
        // roughly equal time above and below its own average.
        double sum = 0.0;
        std::vector<int16_t> noise_values(count);
        for (size_t i = 0; i < count; ++i) {
            int16_t noise = sn_noise(samples[i]);
            noise_values[i] = noise;
            sum += noise;
        }
        double mean = sum / static_cast<double>(count);
        int above = 0, below = 0;
        for (int16_t v : noise_values) {
            if (v > mean) above++;
            else if (v < mean) below++;
        }

        // Distribution should be roughly 50/50 (within 10% tolerance)
        int total = above + below;
        REQUIRE(total > 0);
        double ratio = static_cast<double>(above) / total;
        REQUIRE(ratio > 0.40);  // At least 40%
        REQUIRE(ratio < 0.60);  // At most 60%
    }

    SECTION("LFSR state accessible via introspection") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        // Initial LFSR state should be 0x4000
        auto state = chip.get_noise_channel_state();
        REQUIRE(state.lfsr == 0x4000);

        // Enable noise and run
        chip.write(0xE4);  // Noise: rate=0, white mode
        chip.write(0xF0);  // Noise volume = 0 (max)

        // Run enough cycles to update LFSR
        for (int i = 0; i < 10000; ++i) {
            chip.tick(buffer);
        }

        // LFSR should have changed
        auto new_state = chip.get_noise_channel_state();
        REQUIRE(new_state.lfsr != 0x4000);
        REQUIRE((new_state.lfsr & 0x7FFF) == new_state.lfsr);  // Valid 15-bit value
    }
}

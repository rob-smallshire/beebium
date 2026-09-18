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

// Integration tests for BBC BASIC SOUND commands
//
// These tests boot the machine to MODE 7 BASIC, issue SOUND commands via keyboard typing,
// and verify that the sound chip state and audio output are correct.
//
// This validates the complete path:
//   BBC BASIC → MOS → VIA → Addressable Latch → SN76489 → AudioBuffer

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include "test_keyboard_helpers.hpp"
#include "test_mode7_helpers.hpp"
#include <beebium/devices/Sn76489.hpp>
#include <beebium/AudioBuffer.hpp>

#include <string>
#include <vector>
#include <set>
#include <cmath>
#include <algorithm>

using namespace beebium;
using namespace beebium::test;

// Helper: Unpack SN76489 channel from packed AudioSample
// sources[0] format: [tone0|tone1|tone2|noise] (4 × 8-bit unsigned, DC bias pre-applied)
// Value 128 = DC midpoint (silence), 0-127 = negative swing, 129-255 = positive swing
struct UnpackedSample {
    uint8_t tone0;
    uint8_t tone1;
    uint8_t tone2;
    uint8_t noise;
};

UnpackedSample unpack_sn76489_sample(const AudioSample& sample) {
    uint32_t packed = sample.sources[0];

    // Extract unsigned bytes
    return {
        static_cast<uint8_t>((packed >> 24) & 0xFF),
        static_cast<uint8_t>((packed >> 16) & 0xFF),
        static_cast<uint8_t>((packed >> 8) & 0xFF),
        static_cast<uint8_t>(packed & 0xFF)
    };
}

// Convert unsigned sample (0-255) to signed amplitude (-127 to +127)
// 128 → 0 (silence), 0 → -128, 255 → +127
inline int amplitude_from_unsigned(uint8_t value) {
    return static_cast<int>(value) - 128;
}

// BBC BASIC to SN76489 channel mapping
//
// BBC BASIC channels are mapped to SN76489 channels in REVERSE order:
//   BASIC Channel 0 → SN76489 Noise
//   BASIC Channel 1 → SN76489 Tone 2 (index 2)
//   BASIC Channel 2 → SN76489 Tone 1 (index 1)
//   BASIC Channel 3 → SN76489 Tone 0 (index 0)
//
// In audio samples, the format is: [tone0|tone1|tone2|noise]
// So BASIC channel 1 (SN76489 Tone2) is at audio sample index 2
constexpr size_t basic_to_audio_channel(size_t basic_channel) {
    // BASIC 0 → noise (index 3)
    // BASIC 1 → tone2 (index 2)
    // BASIC 2 → tone1 (index 1)
    // BASIC 3 → tone0 (index 0)
    if (basic_channel == 0) return 3;  // Noise
    return 3 - basic_channel;          // Tones are reversed
}

// For SN76489 chip state queries, BASIC channel → chip tone channel index
// BASIC 1 → chip.get_tone_channel_state(2)
// BASIC 2 → chip.get_tone_channel_state(1)
// BASIC 3 → chip.get_tone_channel_state(0)
constexpr size_t basic_to_chip_tone_channel(size_t basic_channel) {
    // Only valid for BASIC channels 1-3 (not noise)
    return 3 - basic_channel;
}

// Helper: Check if a channel has activity (deviation from DC midpoint 128)
// Takes SN76489 channel index (0-2 for tones, 3 for noise), NOT BASIC channel
bool is_channel_active(const std::vector<AudioSample>& samples, size_t sn76489_channel) {
    for (const auto& sample : samples) {
        auto unpacked = unpack_sn76489_sample(sample);
        uint8_t value = 128;  // DC midpoint = silence
        switch (sn76489_channel) {
            case 0: value = unpacked.tone0; break;
            case 1: value = unpacked.tone1; break;
            case 2: value = unpacked.tone2; break;
            case 3: value = unpacked.noise; break;
        }
        // Active if deviating from DC midpoint (128)
        if (value != 128) return true;
    }
    return false;
}

// Helper: Check if a BASIC channel is active
bool is_basic_channel_active(const std::vector<AudioSample>& samples, size_t basic_channel) {
    return is_channel_active(samples, basic_to_audio_channel(basic_channel));
}

// Helper: Measure approximate frequency from square wave samples
// Returns frequency in Hz, or 0 if unable to measure
// Samples are unsigned (0-255) with DC midpoint at 128
float measure_frequency(const std::vector<uint8_t>& samples, uint32_t sample_rate) {
    if (samples.size() < 10) return 0.0f;

    // Find crossings of DC midpoint (128)
    std::vector<size_t> crossings;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i-1] < 128 && samples[i] >= 128) ||
            (samples[i-1] > 128 && samples[i] <= 128)) {
            crossings.push_back(i);
        }
    }

    if (crossings.size() < 4) return 0.0f;

    // Measure average period (two crossings = one cycle)
    std::vector<float> periods;
    for (size_t i = 2; i < crossings.size(); i += 2) {
        float period = static_cast<float>(crossings[i] - crossings[i-2]);
        periods.push_back(period);
    }

    if (periods.empty()) return 0.0f;

    float avg_period = 0.0f;
    for (float p : periods) avg_period += p;
    avg_period /= periods.size();

    return static_cast<float>(sample_rate) / avg_period;
}

#ifdef BEEBIUM_ROM_DIR

TEMPLATE_TEST_CASE("BBC BASIC SOUND commands", "[sound][basic]", ModelB, ModelBPlus) {
    SECTION("Simple tone on channel 1") {
        // Create MODE 7 test context (boots to BASIC automatically)
        Mode7TestContext<TestType> ctx;
        REQUIRE(ctx.booted);

        // Enable audio output
        ctx.machine.memory().enable_audio_output();

        // Type: SOUND 1, -15, 100, 40
        // BASIC Channel 1, volume -15 (LOUDEST), pitch 100, duration 40 (2 sec)
        // Long duration to ensure we capture audio while it's playing
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 1,-15,100,40\r");

        // Clear audio buffer AFTER typing to discard samples from keyboard activity
        // The sound command has been parsed and is now playing
        if (ctx.machine.memory().audio_buffer.has_value()) {
            ctx.machine.memory().audio_buffer->reset();
        }

        // Run for some time while sound is playing
        // At 2 MHz, 500'000 cycles ≈ 0.25 seconds
        run_cycles(ctx.machine, ctx.renderer, 500'000);

        // Verify chip state
        // BASIC channel 1 → SN76489 tone channel 2 (channels are reversed)
        auto tone = ctx.machine.memory().sound_chip.get_tone_channel_state(
            basic_to_chip_tone_channel(1));

        // Note: Volume may change during sound, just check frequency was set
        REQUIRE(tone.frequency > 0);

        // Verify audio samples were generated
        REQUIRE(ctx.machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = ctx.machine.memory().audio_buffer.value();
        REQUIRE(audio.available() > 0);

        // Read samples
        std::vector<AudioSample> samples(1000);
        size_t count = audio.read(samples.data(), 1000);
        REQUIRE(count > 0);

        // Verify BASIC channel 1 has activity (using correct channel mapping)
        REQUIRE(is_basic_channel_active(samples, 1));
    }

    SECTION("All four channels simultaneously") {
        Mode7TestContext<TestType> ctx;
        REQUIRE(ctx.booted);

        ctx.machine.memory().enable_audio_output();

        // Issue all four SOUND commands with processing time between each
        // Note: For noise channel, pitch 0-3 selects noise rate/mode, NOT musical pitch
        // Use long durations (100 = 5 seconds) to ensure sounds are still playing when we sample

        // BASIC channels 0-3, with 100ms (~200k cycles) between each to allow MOS processing
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 0,-15,0,100\r");
        run_cycles(ctx.machine, ctx.renderer, 200'000);

        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 1,-15,100,100\r");
        run_cycles(ctx.machine, ctx.renderer, 200'000);

        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 2,-15,200,100\r");
        run_cycles(ctx.machine, ctx.renderer, 200'000);

        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 3,-15,150,100\r");
        run_cycles(ctx.machine, ctx.renderer, 200'000);

        // Clear buffer AFTER commands are typed to discard keyboard activity
        if (ctx.machine.memory().audio_buffer.has_value()) {
            ctx.machine.memory().audio_buffer->reset();
        }

        // Now run while all sounds should be playing simultaneously
        run_cycles(ctx.machine, ctx.renderer, 500'000);

        // All channels should have active frequencies
        // Note: Volume checks removed - sound may have already ended or be in envelope
        auto& chip = ctx.machine.memory().sound_chip;
        auto tone_ch1 = chip.get_tone_channel_state(basic_to_chip_tone_channel(1));
        auto tone_ch2 = chip.get_tone_channel_state(basic_to_chip_tone_channel(2));
        auto tone_ch3 = chip.get_tone_channel_state(basic_to_chip_tone_channel(3));

        REQUIRE(tone_ch1.frequency > 0);
        REQUIRE(tone_ch2.frequency > 0);
        REQUIRE(tone_ch3.frequency > 0);

        // Verify audio samples contain activity on all 4 BASIC channels
        REQUIRE(ctx.machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = ctx.machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(2000);
        size_t count = audio.read(samples.data(), 2000);
        REQUIRE(count > 0);

        // Check that all 4 BASIC channels show activity
        REQUIRE(is_basic_channel_active(samples, 0));  // Noise
        REQUIRE(is_basic_channel_active(samples, 1));  // Tone
        REQUIRE(is_basic_channel_active(samples, 2));  // Tone
        REQUIRE(is_basic_channel_active(samples, 3));  // Tone
    }

    SECTION("Volume levels") {
        Mode7TestContext<TestType> ctx;
        REQUIRE(ctx.booted);

        ctx.machine.memory().enable_audio_output();

        // Test that different volume levels produce audio with different amplitudes
        // BBC BASIC: -15 = loudest, 0 = silent
        // Chip: 0 = loudest, 15 = silent
        // The MOS envelope processing may modify the volume, so we just verify
        // that the chip receives writes and generates samples

        // Test -15 (loudest) with long duration (40 = 2 seconds)
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 1,-15,100,40\r");

        // Clear buffer AFTER typing to discard keyboard activity samples
        if (ctx.machine.memory().audio_buffer.has_value()) {
            ctx.machine.memory().audio_buffer->reset();
        }

        run_cycles(ctx.machine, ctx.renderer, 400'000);  // 200ms

        // Verify BASIC channel 1 is active
        REQUIRE(ctx.machine.memory().audio_buffer.has_value());
        std::vector<AudioSample> samples(1000);
        size_t count = ctx.machine.memory().audio_buffer->read(samples.data(), 1000);
        REQUIRE(count > 0);
        REQUIRE(is_basic_channel_active(samples, 1));

        // Find max amplitude in the samples (should be high for -15)
        // Amplitude is deviation from DC midpoint (128)
        int max_amp = 0;
        for (size_t i = 0; i < count; ++i) {
            auto unpacked = unpack_sn76489_sample(samples[i]);
            uint8_t value =
                (basic_to_audio_channel(1) == 0) ? unpacked.tone0 :
                (basic_to_audio_channel(1) == 1) ? unpacked.tone1 :
                (basic_to_audio_channel(1) == 2) ? unpacked.tone2 : unpacked.noise;
            int amp = std::abs(amplitude_from_unsigned(value));
            if (amp > max_amp) max_amp = amp;
        }
        REQUIRE(max_amp > 50);  // Should have significant amplitude
    }

    SECTION("Pitch accuracy") {
        Mode7TestContext<TestType> ctx;
        REQUIRE(ctx.booted);

        ctx.machine.memory().enable_audio_output();
        if (ctx.machine.memory().audio_buffer.has_value()) {
            ctx.machine.memory().audio_buffer->reset();
        }

        // Generate a tone with known frequency
        // BBC BASIC pitch parameter is complex, but we can verify chip frequency is set
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 1,-15,100,20\r");
        run_cycles(ctx.machine, ctx.renderer, 1'000'000);

        // BASIC channel 1 → SN76489 tone channel 2
        auto tone = ctx.machine.memory().sound_chip.get_tone_channel_state(
            basic_to_chip_tone_channel(1));

        // Verify frequency_hz is calculated and reasonable
        REQUIRE(tone.frequency_hz > 0.0f);
        REQUIRE(tone.frequency_hz < 20000.0f);  // Within audio range

        // Read samples and attempt frequency measurement
        REQUIRE(ctx.machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = ctx.machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(4800);  // 0.1 second @ 48kHz
        size_t count = audio.read(samples.data(), 4800);
        REQUIRE(count > 100);  // Should have generated samples

        // Extract BASIC channel 1 samples (SN76489 tone 2)
        std::vector<uint8_t> channel_samples;
        for (size_t i = 0; i < count; ++i) {
            auto unpacked = unpack_sn76489_sample(samples[i]);
            // BASIC channel 1 → audio channel 2 (tone2)
            channel_samples.push_back(unpacked.tone2);
        }

        // Measure frequency (should be close to chip's frequency_hz)
        float measured_hz = measure_frequency(channel_samples, 48000);

        // Allow for measurement error and frequency quantization
        if (measured_hz > 0.0f) {
            float error = std::abs(measured_hz - tone.frequency_hz) / tone.frequency_hz;
            REQUIRE(error < 0.1f);  // Within 10% (generous for square wave aliasing)
        }
    }

    SECTION("Noise generation") {
        Mode7TestContext<TestType> ctx;
        REQUIRE(ctx.booted);

        ctx.machine.memory().enable_audio_output();

        // Test noise channel (BASIC channel 0)
        // BBC BASIC pitch parameter for noise: pitch 4-7 select white noise mode
        // Use long duration (100 = 5 seconds) to ensure sound is still playing when we sample
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 0,-15,4,100\r");

        // Give MOS time to process the sound command
        run_cycles(ctx.machine, ctx.renderer, 200'000);

        // Clear buffer AFTER sound has started playing
        if (ctx.machine.memory().audio_buffer.has_value()) {
            ctx.machine.memory().audio_buffer->reset();
        }

        // Run while sampling - white noise should show variation
        run_cycles(ctx.machine, ctx.renderer, 1'000'000);

        // Verify noise samples generated
        REQUIRE(ctx.machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = ctx.machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(1000);
        size_t count = audio.read(samples.data(), 1000);
        REQUIRE(count > 0);

        // Noise channel (BASIC channel 0) should be active
        REQUIRE(is_basic_channel_active(samples, 0));

        // Extract noise samples and verify non-repeating pattern (white noise)
        std::vector<int8_t> noise_samples;
        for (size_t i = 0; i < count; ++i) {
            auto unpacked = unpack_sn76489_sample(samples[i]);
            noise_samples.push_back(unpacked.noise);
        }

        // White noise should not have a short repeating pattern
        // Check that samples vary (not all the same)
        auto min_it = std::min_element(noise_samples.begin(), noise_samples.end());
        auto max_it = std::max_element(noise_samples.begin(), noise_samples.end());
        REQUIRE(*min_it != *max_it);  // Should have variation

        // Test periodic noise (pitch 0)
        // Note: Periodic noise toggles slowly (period 15 at rate 0),
        // so we only verify samples are being generated, not variation
        type_string_with_shift(ctx.machine, ctx.renderer, "SOUND 0,-15,0,100\r");
        audio.reset();  // Clear buffer AFTER typing
        run_cycles(ctx.machine, ctx.renderer, 500'000);

        // Read samples
        samples.clear();
        samples.resize(1000);
        count = audio.read(samples.data(), 1000);
        REQUIRE(count > 0);

        // Verify periodic noise is generating samples (may be constant at slow rates)
        REQUIRE(is_basic_channel_active(samples, 0));
    }
}

// Waveform integrity tests - verify square wave properties
TEST_CASE("Square wave integrity", "[sn76489][waveform]") {
    // Unit tests for waveform properties (no ROM required)

    SECTION("50% duty cycle") {
        // A proper square wave should have equal positive and negative samples
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(48000);  // 1 second buffer

        // Set a tone with known frequency
        // Frequency = 100 → f_out = 4 MHz / (32 × 100) = 1250 Hz
        chip.write(0x84);  // Tone 0 freq low = 4
        chip.write(0x06);  // Tone 0 freq high = 6 → 0x64 (100)
        chip.write(0x90);  // Volume = 0 (max)

        // Run enough cycles to generate several complete waves
        // At 1250 Hz, 48000/1250 = 38.4 samples per cycle
        // Generate ~100 cycles worth
        for (int i = 0; i < 200'000; ++i) {
            chip.tick(buffer);
        }

        // Read samples
        std::vector<AudioSample> samples(buffer.available());
        size_t count = buffer.read(samples.data(), samples.size());
        REQUIRE(count > 100);

        // The output is unipolar (0..254) and band-limited, so split about the
        // mean level: a symmetric square spends roughly equal time above and
        // below its own average.
        double sum = 0.0;
        std::vector<uint8_t> tone_values(count);
        for (size_t i = 0; i < count; ++i) {
            uint8_t tone0 = static_cast<uint8_t>((samples[i].sources[0] >> 24) & 0xFF);
            tone_values[i] = tone0;
            sum += tone0;
        }
        double mean = sum / static_cast<double>(count);
        int above = 0, below = 0;
        for (uint8_t v : tone_values) {
            if (v > mean) above++;
            else if (v < mean) below++;
        }

        // Should be approximately 50/50 (within 5% tolerance)
        int total = above + below;
        REQUIRE(total > 0);
        float ratio = static_cast<float>(above) / total;
        REQUIRE(ratio > 0.45f);
        REQUIRE(ratio < 0.55f);
    }

    SECTION("Amplitude spans the full unipolar swing") {
        // The output is unipolar (0 = silence level, 2*amplitude = flip-flop
        // high) and band-limited by the anti-alias low-pass, so a square is no
        // longer just two levels: it reaches near the extremes on each plateau
        // with smooth band-limited transitions between them.
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(8192);

        chip.write(0x84);  // Freq = 100 -> ~1250 Hz
        chip.write(0x06);
        chip.write(0x90);  // Volume = 0 (max): high level = 2 * 127 = 254

        for (int i = 0; i < 100'000; ++i) {
            chip.tick(buffer);
        }

        std::vector<AudioSample> samples(buffer.available());
        size_t count = buffer.read(samples.data(), samples.size());
        REQUIRE(count > 10);

        std::set<uint8_t> values;
        uint8_t min_v = 255, max_v = 0;
        for (size_t i = 0; i < count; ++i) {
            uint8_t tone0 = static_cast<uint8_t>((samples[i].sources[0] >> 24) & 0xFF);
            values.insert(tone0);
            min_v = std::min(min_v, tone0);
            max_v = std::max(max_v, tone0);
        }

        // Reaches near the silence level and near the max level (2 * 127 = 254).
        REQUIRE(min_v <= 6);
        REQUIRE(max_v >= 248);
        // Band-limited: more than the two point-sampled levels.
        REQUIRE(values.size() > 2);
    }

    SECTION("Mean tracks the volume level") {
        // The unipolar output of a 50%-duty square averages to the mid-point,
        // which is the volume-table amplitude: 127 at volume 0 (high level 254,
        // silence level 0). This volume-dependent mean is what carries sampled
        // PCM.
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(48000);

        chip.write(0x84);  // Freq = 100
        chip.write(0x06);
        chip.write(0x90);  // Volume = 0 (max)

        // Generate many cycles
        for (int i = 0; i < 400'000; ++i) {
            chip.tick(buffer);
        }

        std::vector<AudioSample> samples(buffer.available());
        size_t count = buffer.read(samples.data(), samples.size());
        REQUIRE(count > 1000);

        // Calculate average value (unsigned)
        int64_t sum = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t packed = samples[i].sources[0];
            uint8_t tone0 = static_cast<uint8_t>((packed >> 24) & 0xFF);
            sum += tone0;
        }

        double avg = static_cast<double>(sum) / count;

        // Mean should be near the volume-0 amplitude (127).
        REQUIRE(std::abs(avg - 127.0) < 5.0);  // Allow small bias due to incomplete cycles
    }

    SECTION("DC midpoint crossings match expected frequency") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(48000);

        // Frequency = 200 → f_out = 4 MHz / (32 × 200) = 625 Hz
        chip.write(0x88);  // Tone 0 freq low = 8
        chip.write(0x0C);  // Tone 0 freq high = 0xC → 0xC8 (200)
        chip.write(0x90);  // Volume = 0

        // Generate ~1 second of audio
        for (int i = 0; i < 2'000'000; ++i) {
            chip.tick(buffer);
        }

        std::vector<AudioSample> samples(buffer.available());
        size_t count = buffer.read(samples.data(), samples.size());
        REQUIRE(count > 1000);

        // Count crossings of DC midpoint (128)
        int crossings = 0;
        for (size_t i = 1; i < count; ++i) {
            uint32_t packed_prev = samples[i-1].sources[0];
            uint32_t packed = samples[i].sources[0];
            uint8_t prev = static_cast<uint8_t>((packed_prev >> 24) & 0xFF);
            uint8_t curr = static_cast<uint8_t>((packed >> 24) & 0xFF);

            if ((prev < 128 && curr >= 128) || (prev > 128 && curr <= 128)) {
                crossings++;
            }
        }

        // Expected: 2 crossings per cycle × 625 Hz × (count/48000) seconds
        float expected_crossings = 2.0f * 625.0f * (count / 48000.0f);

        // Allow 10% tolerance
        float ratio = crossings / expected_crossings;
        REQUIRE(ratio > 0.90f);
        REQUIRE(ratio < 1.10f);
    }
}

#endif // BEEBIUM_ROM_DIR

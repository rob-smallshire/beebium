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

// Edge case tests for SN76489 sound chip
//
// These tests cover unusual scenarios discovered from other emulators:
// - beebjit's Speech.dsd: writes without toggling sound write enable
// - beebjit's ReetPetite.ssd: very fast period=4 playback
// - General: mid-tone frequency changes, simultaneous multi-channel updates
//
// Reference: https://github.com/scarybeasts/beebjit/tree/master/test/sound

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <beebium/Machines.hpp>
#include <beebium/devices/Sn76489.hpp>
#include <beebium/AudioBuffer.hpp>
#include "sn76489_channels.hpp"

#include <vector>
#include <cmath>

using namespace beebium;

// System VIA registers (FE40-FE5F)
constexpr uint16_t VIA_BASE = 0xFE40;
constexpr uint16_t VIA_PORT_B = VIA_BASE + 0x00;
constexpr uint16_t VIA_PORT_A = VIA_BASE + 0x01;
constexpr uint16_t VIA_DDRB = VIA_BASE + 0x02;
constexpr uint16_t VIA_DDRA = VIA_BASE + 0x03;

constexpr uint8_t LATCH_ADDR_SOUND_WRITE = 0;

// Helper: Set addressable latch bit
template<typename MachineType>
void set_latch_bit(MachineType& machine, uint8_t bit_addr, bool value) {
    uint8_t port_b_value = (bit_addr & 0x07) | (value ? 0x08 : 0x00);
    machine.write(VIA_PORT_B, port_b_value);
}

// Helper: Write to SN76489 with proper strobe pattern
template<typename MachineType>
void write_sound_chip(MachineType& machine, uint8_t data) {
    set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);  // Enable
    machine.write(VIA_PORT_A, data);
    for (int i = 0; i < 10; ++i) machine.step();
    set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);   // Disable
}

// Helper: Unpack SN76489 channels from AudioSample (2x16 encoding, silence = 0)
struct UnpackedSample {
    int16_t tone0;
    int16_t tone1;
    int16_t tone2;
    int16_t noise;
};

UnpackedSample unpack_sn76489_sample(const AudioSample& sample) {
    return {sn_tone0(sample), sn_tone1(sample), sn_tone2(sample), sn_noise(sample)};
}

// Helper: Check if channel has activity (deviation from the silence level 0)
bool has_channel_activity(const std::vector<AudioSample>& samples, size_t channel_index) {
    for (const auto& sample : samples) {
        auto unpacked = unpack_sn76489_sample(sample);
        int16_t amp = 0;
        switch (channel_index) {
            case 0: amp = unpacked.tone0; break;
            case 1: amp = unpacked.tone1; break;
            case 2: amp = unpacked.tone2; break;
            case 3: amp = unpacked.noise; break;
        }
        if (amp != 0) return true;
    }
    return false;
}

TEMPLATE_TEST_CASE("SN76489 edge cases", "[sound][edge]", ModelB, ModelBPlus) {
    using MachineType = TestType;

    SECTION("Writes blocked when sound write enable not toggled (Speech.dsd pattern)") {
        // Inspired by beebjit's Speech.dsd test
        // Some programs write to Port A without toggling the addressable latch
        // These writes should be ignored by the sound chip

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        // Ensure sound write is disabled
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);
        for (int i = 0; i < 20; ++i) machine.step();

        // Record initial chip state
        auto initial = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(initial.frequency == 0);
        REQUIRE(initial.volume == 15);  // Silent on reset

        // Write to Port A without enabling sound write
        // (This is the Speech.dsd edge case)
        machine.write(VIA_PORT_A, 0b10001111);  // Tone 0 freq = 0xF
        for (int i = 0; i < 20; ++i) machine.step();

        machine.write(VIA_PORT_A, 0b00111111);  // Freq high bits = 0x3F
        for (int i = 0; i < 20; ++i) machine.step();

        machine.write(VIA_PORT_A, 0b10010000);  // Volume = 0 (max)
        for (int i = 0; i < 20; ++i) machine.step();

        // Chip state should be unchanged
        auto after = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(after.frequency == 0);
        REQUIRE(after.volume == 15);
    }

    SECTION("Rapid period changes (ReetPetite.ssd pattern - period=4)") {
        // Inspired by beebjit's ReetPetite.ssd test
        // Very short period values (e.g., 4) used for sampled sound playback
        // Tests that the chip handles rapid frequency toggling correctly

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        if (machine.memory().audio_buffer.has_value()) {
            machine.memory().audio_buffer->reset();
        }

        // Set tone 0 to minimum period (4) for very high frequency
        // Frequency = 4: f_out = 4 MHz / (32 × 4) = 31.25 kHz
        write_sound_chip(machine, 0b10000100);  // Tone 0 freq low = 4
        write_sound_chip(machine, 0b00000000);  // Tone 0 freq high = 0
        write_sound_chip(machine, 0b10010000);  // Volume = 0 (max)

        auto state = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 4);

        // Expected frequency: 4 MHz / (32 × 4) = 31250 Hz
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(31250.0f, 0.01f));

        // Run and verify samples are generated correctly
        for (int i = 0; i < 100'000; ++i) {
            machine.step();
        }

        REQUIRE(machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(1000);
        size_t count = audio.read(samples.data(), 1000);
        REQUIRE(count > 0);

        // Channel 0 should have activity (high frequency square wave)
        REQUIRE(has_channel_activity(samples, 0));
    }

    SECTION("Mid-tone frequency change") {
        // Change frequency while a tone is already playing
        // Should transition smoothly without glitches

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        // Start with frequency = 100
        write_sound_chip(machine, 0b10000100);  // Freq low = 4
        write_sound_chip(machine, 0b00000110);  // Freq high = 6 → total 0x64 (100)
        write_sound_chip(machine, 0b10010000);  // Volume = 0 (max)

        // Run for a bit
        for (int i = 0; i < 50'000; ++i) {
            machine.step();
        }

        auto state1 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state1.frequency == 100);

        // Change to frequency = 200 mid-playback
        write_sound_chip(machine, 0b10001000);  // Freq low = 8
        write_sound_chip(machine, 0b00001100);  // Freq high = 0xC → total 0xC8 (200)

        auto state2 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state2.frequency == 200);

        // Counter should continue from where it was or reset
        // Either behavior is acceptable, just verify it's valid
        REQUIRE(state2.counter <= 200);

        // Run more and verify samples
        for (int i = 0; i < 50'000; ++i) {
            machine.step();
        }

        REQUIRE(machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = machine.memory().audio_buffer.value();
        REQUIRE(audio.available() > 0);
    }

    SECTION("Simultaneous multi-channel updates") {
        // Update all 4 channels in rapid succession
        // Should handle interleaved writes correctly

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        if (machine.memory().audio_buffer.has_value()) {
            machine.memory().audio_buffer->reset();
        }

        // Rapid writes to all channels (no stepping between)
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);

        // Write all 4 channels rapidly
        machine.write(VIA_PORT_A, 0b10000101);  // Tone 0 freq = 5
        machine.write(VIA_PORT_A, 0b10100111);  // Tone 1 freq = 7
        machine.write(VIA_PORT_A, 0b11001001);  // Tone 2 freq = 9
        machine.write(VIA_PORT_A, 0b11100100);  // Noise: rate=0, white

        machine.write(VIA_PORT_A, 0b10010000);  // Tone 0 vol = 0
        machine.write(VIA_PORT_A, 0b10110010);  // Tone 1 vol = 2
        machine.write(VIA_PORT_A, 0b11010100);  // Tone 2 vol = 4
        machine.write(VIA_PORT_A, 0b11110110);  // Noise vol = 6

        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);

        // Run for some cycles
        for (int i = 0; i < 10; ++i) {
            machine.step();
        }

        // Verify all channels received their writes
        auto tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        auto tone1 = machine.memory().sound_chip.get_tone_channel_state(1);
        auto tone2 = machine.memory().sound_chip.get_tone_channel_state(2);
        auto noise = machine.memory().sound_chip.get_noise_channel_state();

        REQUIRE((tone0.frequency & 0x0F) == 5);
        REQUIRE(tone0.volume == 0);

        REQUIRE((tone1.frequency & 0x0F) == 7);
        REQUIRE(tone1.volume == 2);

        REQUIRE((tone2.frequency & 0x0F) == 9);
        REQUIRE(tone2.volume == 4);

        REQUIRE(noise.rate_select == 0);
        REQUIRE(noise.white_mode == true);
        REQUIRE(noise.volume == 6);
    }

    SECTION("Frequency value of 1 (highest valid frequency)") {
        // Frequency = 1 produces the highest frequency
        // f_out = 4 MHz / (32 × 1) = 125 kHz

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        write_sound_chip(machine, 0b10000001);  // Tone 0 freq = 1
        write_sound_chip(machine, 0b00000000);  // High bits = 0
        write_sound_chip(machine, 0b10010000);  // Volume = 0

        auto state = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 1);
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(125000.0f, 0.01f));

        // Run and verify
        for (int i = 0; i < 50'000; ++i) {
            machine.step();
        }

        REQUIRE(machine.memory().audio_buffer.has_value());
    }

    SECTION("Maximum frequency value (0x3FF)") {
        // Frequency = 1023 produces the lowest non-zero frequency
        // f_out = 4 MHz / (32 × 1023) ≈ 122.18 Hz

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        write_sound_chip(machine, 0b10001111);  // Tone 0 freq low = 0xF
        write_sound_chip(machine, 0b00111111);  // High bits = 0x3F → 0x3FF
        write_sound_chip(machine, 0b10010000);  // Volume = 0

        auto state = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state.frequency == 0x3FF);
        REQUIRE_THAT(state.frequency_hz, Catch::Matchers::WithinRel(122.18f, 0.01f));
    }

    SECTION("Noise clocked by tone 2 (rate=3)") {
        // When noise rate=3, it's clocked by tone channel 2's output
        // This is used for special effects

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        if (machine.memory().audio_buffer.has_value()) {
            machine.memory().audio_buffer->reset();
        }

        // Set tone 2 to a specific frequency
        write_sound_chip(machine, 0b11000100);  // Tone 2 freq low = 4
        write_sound_chip(machine, 0b00000110);  // Tone 2 freq high = 6 → 0x64 (100)
        write_sound_chip(machine, 0b11011111);  // Tone 2 vol = 15 (silent)

        // Set noise to use tone 2 as clock (rate=3)
        write_sound_chip(machine, 0b11100111);  // Noise: rate=3, white mode
        write_sound_chip(machine, 0b11110000);  // Noise vol = 0 (max)

        auto noise = machine.memory().sound_chip.get_noise_channel_state();
        REQUIRE(noise.rate_select == 3);
        REQUIRE(noise.white_mode == true);

        // Noise rate should match tone 2 frequency
        auto tone2 = machine.memory().sound_chip.get_tone_channel_state(2);
        REQUIRE_THAT(noise.rate_hz, Catch::Matchers::WithinRel(tone2.frequency_hz, 0.01f));

        // Run and verify noise is generated
        for (int i = 0; i < 200'000; ++i) {
            machine.step();
        }

        REQUIRE(machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(1000);
        size_t count = audio.read(samples.data(), 1000);
        REQUIRE(count > 0);

        // Noise channel should have activity
        REQUIRE(has_channel_activity(samples, 3));
    }

    SECTION("Volume change during playback") {
        // Change volume while a tone is playing
        // Should take effect immediately

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        // Start with max volume
        write_sound_chip(machine, 0b10000100);  // Freq = 100
        write_sound_chip(machine, 0b00000110);
        write_sound_chip(machine, 0b10010000);  // Volume = 0 (max)

        for (int i = 0; i < 20'000; ++i) {
            machine.step();
        }

        auto state1 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state1.volume == 0);
        int8_t amp1 = state1.amplitude;

        // Change to half volume
        write_sound_chip(machine, 0b10010111);  // Volume = 7

        auto state2 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state2.volume == 7);
        int8_t amp2 = state2.amplitude;

        // Amplitude should be lower (volume 7 is quieter than volume 0)
        REQUIRE(std::abs(amp2) < std::abs(amp1));

        // Change to silence
        write_sound_chip(machine, 0b10011111);  // Volume = 15

        auto state3 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(state3.volume == 15);
        REQUIRE(state3.amplitude == 0);
    }
}

TEST_CASE("SN76489 unit edge cases", "[sn76489][edge]") {
    // Tests that don't need full machine integration

    SECTION("Write trace captures all writes") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        chip.clear_write_trace();

        // Write several commands
        chip.write(0x80);  // Latch tone 0 freq
        chip.write(0x10);  // Data byte
        chip.write(0x90);  // Latch tone 0 vol

        REQUIRE(chip.get_write_trace_count() == 3);

        auto* trace = chip.get_write_trace();
        REQUIRE(trace[0].data == 0x80);
        REQUIRE(trace[0].is_latch == true);

        REQUIRE(trace[1].data == 0x10);
        REQUIRE(trace[1].is_latch == false);

        REQUIRE(trace[2].data == 0x90);
        REQUIRE(trace[2].is_latch == true);
    }

    SECTION("Reset clears LFSR to initial state") {
        Sn76489 chip(4'000'000, 48'000);
        AudioBuffer buffer(1024);

        // Enable noise and run to change LFSR
        chip.write(0xE4);  // Noise white mode
        chip.write(0xF0);  // Noise volume = 0

        for (int i = 0; i < 10000; ++i) {
            chip.tick(buffer);
        }

        auto state_before = chip.get_noise_channel_state();
        REQUIRE(state_before.lfsr != 0x4000);  // Should have changed

        // Reset
        chip.reset();

        auto state_after = chip.get_noise_channel_state();
        REQUIRE(state_after.lfsr == 0x4000);  // Back to initial
        REQUIRE(state_after.volume == 15);     // Silent
    }

    SECTION("Latched register persists correctly") {
        Sn76489 chip(4'000'000, 48'000);

        // Latch tone 0 freq
        chip.write(0x80);
        REQUIRE(chip.latched_register() == 0);

        // Send data bytes (should use latched register)
        chip.write(0x10);
        REQUIRE(chip.latched_register() == 0);  // Still latched to reg 0

        chip.write(0x20);
        REQUIRE(chip.latched_register() == 0);

        // Now latch a different register
        chip.write(0xA0);  // Tone 1 freq
        REQUIRE(chip.latched_register() == 2);

        // Data byte goes to tone 1
        chip.write(0x15);
        REQUIRE(chip.latched_register() == 2);

        // Verify frequencies
        auto tone0 = chip.get_tone_channel_state(0);
        auto tone1 = chip.get_tone_channel_state(1);

        // Tone 0: latch wrote 0 low bits, data wrote 0x20 high bits
        REQUIRE((tone0.frequency & 0x3F0) == 0x200);  // High bits from last data byte

        // Tone 1: latch wrote 0 low bits, data wrote 0x15 high bits
        REQUIRE((tone1.frequency & 0x3F0) == 0x150);
    }
}

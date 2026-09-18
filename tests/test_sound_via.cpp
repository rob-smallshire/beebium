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

// Integration tests for SN76489 via direct System VIA writes
//
// These tests bypass BBC BASIC and MOS, writing directly to:
//   - System VIA Port B (addressable latch control)
//   - System VIA Port A (sound chip data)
//
// This validates the hardware path:
//   Direct VIA writes → Addressable Latch → SN76489 → AudioBuffer

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/devices/Sn76489.hpp>
#include <beebium/AudioBuffer.hpp>

#include <vector>

using namespace beebium;

// System VIA registers (FE40-FE5F)
constexpr uint16_t VIA_BASE = 0xFE40;
constexpr uint16_t VIA_PORT_B = VIA_BASE + 0x00;  // FE40 - Port B (addressable latch control)
constexpr uint16_t VIA_PORT_A = VIA_BASE + 0x01;  // FE41 - Port A (sound chip data)
constexpr uint16_t VIA_DDRB = VIA_BASE + 0x02;    // FE42 - Port B direction
constexpr uint16_t VIA_DDRA = VIA_BASE + 0x03;    // FE43 - Port A direction

// Addressable latch addresses (written to Port B bits 0-2)
constexpr uint8_t LATCH_ADDR_SOUND_WRITE = 0;  // Bit 0: Sound write enable (active low)

// Helper: Set addressable latch bit
template<typename MachineType>
void set_latch_bit(MachineType& machine, uint8_t bit_addr, bool value) {
    // Addressable latch control via Port B:
    // Bits 0-2: address (which latch bit to set)
    // Bit 3: data (value to set)
    uint8_t port_b_value = (bit_addr & 0x07) | (value ? 0x08 : 0x00);
    machine.write(VIA_PORT_B, port_b_value);
}

// Helper: Write to SN76489 via System VIA Port A
template<typename MachineType>
void write_sound_chip(MachineType& machine, uint8_t data) {
    // Enable sound write (addressable latch bit 0 = 0, active low)
    set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);

    // Write to Port A
    machine.write(VIA_PORT_A, data);

    // Run a few cycles to allow write to propagate
    for (int i = 0; i < 10; ++i) {
        machine.step();
    }

    // Disable sound write (addressable latch bit 0 = 1)
    set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);
}

// Helper: Unpack SN76489 channels from AudioSample
// Samples are unsigned (0-255) with DC midpoint at 128
struct UnpackedSample {
    uint8_t tone0;
    uint8_t tone1;
    uint8_t tone2;
    uint8_t noise;
};

UnpackedSample unpack_sn76489_sample(const AudioSample& sample) {
    uint32_t packed = sample.sources[0];

    return {
        static_cast<uint8_t>((packed >> 24) & 0xFF),
        static_cast<uint8_t>((packed >> 16) & 0xFF),
        static_cast<uint8_t>((packed >> 8) & 0xFF),
        static_cast<uint8_t>(packed & 0xFF)
    };
}

// Helper: Check if channel has activity (deviation from the silence level 0).
// The output is unipolar: a silent channel sits at 0, an active one oscillates
// above it.
bool has_channel_activity(const std::vector<AudioSample>& samples, size_t channel_index) {
    for (const auto& sample : samples) {
        auto unpacked = unpack_sn76489_sample(sample);
        uint8_t value = 0;  // silence level
        switch (channel_index) {
            case 0: value = unpacked.tone0; break;
            case 1: value = unpacked.tone1; break;
            case 2: value = unpacked.tone2; break;
            case 3: value = unpacked.noise; break;
        }
        if (value != 0) return true;
    }
    return false;
}

TEMPLATE_TEST_CASE("SN76489 via System VIA integration", "[sound][via]", ModelB, ModelBPlus) {
    using MachineType = TestType;

    SECTION("Direct tone generation on channel 0") {
        MachineType machine;
        machine.memory().enable_audio_output();

        // Configure VIA ports as outputs
        machine.write(VIA_DDRB, 0xFF);  // Port B all outputs
        machine.write(VIA_DDRA, 0xFF);  // Port A all outputs

        // Clear audio buffer
        if (machine.memory().audio_buffer.has_value()) {
            machine.memory().audio_buffer->reset();
        }

        // SN76489 register write protocol:
        // Latch byte: 1cctdddd (bit 7=1, cc=channel, t=type, dddd=data low 4 bits)
        // Data byte:  0DDDDDD  (bit 7=0, DDDDDD=data high 6 bits)

        // Write tone 0 frequency = 0x100 (256 decimal)
        // Frequency is 10-bit: latch has bits 3-0, data has bits 9-4
        // 0x100 = 0b0100000000 → lower 4 bits = 0x0, upper 6 bits = 0x10
        // Latch byte: 10000000 (channel 0, tone, lower 4 bits = 0)
        write_sound_chip(machine, 0b10000000);

        // Data byte: 00010000 (upper 6 bits = 0x10, total frequency = 0x100)
        write_sound_chip(machine, 0b00010000);

        // Write tone 0 volume = 0 (maximum volume)
        // Latch byte: 10010000 (channel 0, volume, value = 0)
        write_sound_chip(machine, 0b10010000);

        // Run machine to generate samples
        // At 2 MHz, 500000 cycles = 0.25 seconds = 12000 samples @ 48kHz
        for (int i = 0; i < 500'000; ++i) {
            machine.step();
        }

        // Verify chip state
        auto tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(tone0.frequency == 0x100);
        REQUIRE(tone0.volume == 0);  // Maximum volume

        // Verify audio samples were generated
        REQUIRE(machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = machine.memory().audio_buffer.value();
        REQUIRE(audio.available() > 0);

        // Read samples
        std::vector<AudioSample> samples(1000);
        size_t count = audio.read(samples.data(), 1000);
        REQUIRE(count > 0);

        // Verify channel 0 is active
        REQUIRE(has_channel_activity(samples, 0));

        // Verify other channels are silent
        REQUIRE_FALSE(has_channel_activity(samples, 1));
        REQUIRE_FALSE(has_channel_activity(samples, 2));
        REQUIRE_FALSE(has_channel_activity(samples, 3));
    }

    SECTION("All four channels active") {
        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        if (machine.memory().audio_buffer.has_value()) {
            machine.memory().audio_buffer->reset();
        }

        // Configure tone channel 0: freq=0x100, vol=0
        write_sound_chip(machine, 0b10000000);  // Tone 0 freq low (bits 3-0 = 0x0)
        write_sound_chip(machine, 0b00010000);  // Tone 0 freq high (bits 9-4 = 0x10)
        write_sound_chip(machine, 0b10010000);  // Tone 0 vol=0 (max)

        // Configure tone channel 1: freq=0x200, vol=5
        write_sound_chip(machine, 0b10100000);  // Tone 1 freq low (bits 3-0 = 0x0)
        write_sound_chip(machine, 0b00100000);  // Tone 1 freq high (bits 9-4 = 0x20)
        write_sound_chip(machine, 0b10110101);  // Tone 1 vol=5

        // Configure tone channel 2: freq=0x300, vol=10
        write_sound_chip(machine, 0b11000000);  // Tone 2 freq low (bits 3-0 = 0x0)
        write_sound_chip(machine, 0b00110000);  // Tone 2 freq high (bits 9-4 = 0x30)
        write_sound_chip(machine, 0b11011010);  // Tone 2 vol=10

        // Configure noise channel: white noise, rate=1, vol=0
        // Noise control: bits 1-0=rate, bit 2=white mode
        // White noise, rate 1: (1 << 0) | (1 << 2) = 0b101
        write_sound_chip(machine, 0b11100101);  // Noise control (white=1, rate=1)
        write_sound_chip(machine, 0b11110000);  // Noise vol=0 (max)

        // Run machine
        for (int i = 0; i < 500'000; ++i) {
            machine.step();
        }

        // Verify all channels configured
        auto tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        auto tone1 = machine.memory().sound_chip.get_tone_channel_state(1);
        auto tone2 = machine.memory().sound_chip.get_tone_channel_state(2);
        auto noise = machine.memory().sound_chip.get_noise_channel_state();

        REQUIRE(tone0.frequency == 0x100);
        REQUIRE(tone0.volume == 0);

        REQUIRE(tone1.frequency == 0x200);
        REQUIRE(tone1.volume == 5);

        REQUIRE(tone2.frequency == 0x300);
        REQUIRE(tone2.volume == 10);

        REQUIRE(noise.white_mode == true);
        REQUIRE(noise.volume == 0);

        // Verify audio samples contain all channels
        REQUIRE(machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(2000);
        size_t count = audio.read(samples.data(), 2000);
        REQUIRE(count > 0);

        // All 4 channels should be active
        REQUIRE(has_channel_activity(samples, 0));
        REQUIRE(has_channel_activity(samples, 1));
        REQUIRE(has_channel_activity(samples, 2));
        REQUIRE(has_channel_activity(samples, 3));
    }

    SECTION("Addressable latch gating works") {
        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        // Set sound write disabled (latch bit 0 = 1, inactive)
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);

        // Try to write to sound chip (should be blocked)
        machine.write(VIA_PORT_A, 0b10000000);
        machine.write(VIA_PORT_A, 0b00000100);
        machine.write(VIA_PORT_A, 0b10010000);

        // Run briefly
        for (int i = 0; i < 1000; ++i) {
            machine.step();
        }

        // Chip should still be in reset state (all registers 0)
        auto tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(tone0.frequency == 0);  // Should be 0 (not 0x100)

        // Now enable sound write and try again
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);

        machine.write(VIA_PORT_A, 0b10000000);  // Freq low
        for (int i = 0; i < 10; ++i) machine.step();

        machine.write(VIA_PORT_A, 0b00010000);  // Freq high (0x10)
        for (int i = 0; i < 10; ++i) machine.step();

        machine.write(VIA_PORT_A, 0b10010000);  // Vol=0
        for (int i = 0; i < 10; ++i) machine.step();

        // Now chip should have received the writes
        tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(tone0.frequency == 0x100);
        REQUIRE(tone0.volume == 0);
    }

    SECTION("Volume affects sample amplitude") {
        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        // Test different volume levels
        std::vector<uint8_t> volumes = {0, 5, 10, 15};  // 0=max, 15=silence

        for (uint8_t vol : volumes) {
            if (machine.memory().audio_buffer.has_value()) {
                machine.memory().audio_buffer->reset();
            }

            // Set frequency = 0x100
            write_sound_chip(machine, 0b10000000);  // Freq low
            write_sound_chip(machine, 0b00010000);  // Freq high

            // Set volume
            write_sound_chip(machine, 0b10010000 | vol);

            // Run to generate samples
            for (int i = 0; i < 100'000; ++i) {
                machine.step();
            }

            // Check amplitude
            auto tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
            REQUIRE(tone0.volume == vol);

            if (vol == 15) {
                // Silence
                REQUIRE(tone0.amplitude == 0);
            } else {
                // Should have non-zero amplitude
                REQUIRE(tone0.amplitude != 0);

                // Lower volume number = higher amplitude
                if (vol == 0) {
                    REQUIRE(std::abs(tone0.amplitude) > 100);  // Near max (127)
                }
            }
        }
    }

    SECTION("MOS-style strobe write pattern (falling edge)") {
        // The MOS uses a different write pattern than write-while-enabled:
        // 1. Sound write disabled (latch bit 0 = 1)
        // 2. Write data to Port A
        // 3. Enable sound write (latch bit 0 = 0) - falling edge triggers write
        // 4. Disable sound write after >=8µs

        MachineType machine;
        machine.memory().enable_audio_output();

        machine.write(VIA_DDRB, 0xFF);
        machine.write(VIA_DDRA, 0xFF);

        // Start with sound write disabled
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);
        for (int i = 0; i < 10; ++i) machine.step();

        // Write data to Port A (while disabled)
        machine.write(VIA_PORT_A, 0b10000101);  // Tone 0 freq low = 5
        for (int i = 0; i < 10; ++i) machine.step();

        // Enable sound write (falling edge triggers latch)
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);
        for (int i = 0; i < 20; ++i) machine.step();  // Wait for write

        // Disable sound write
        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);
        for (int i = 0; i < 10; ++i) machine.step();

        // Verify chip received the write
        auto tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE((tone0.frequency & 0x0F) == 5);  // Low 4 bits should be 5

        // Do another strobe write for high bits
        machine.write(VIA_PORT_A, 0b00010000);  // Data byte: high bits = 0x10
        for (int i = 0; i < 10; ++i) machine.step();

        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);
        for (int i = 0; i < 20; ++i) machine.step();

        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);
        for (int i = 0; i < 10; ++i) machine.step();

        tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(tone0.frequency == 0x105);  // Full 10-bit value: 0x100 | 5

        // Strobe volume write
        machine.write(VIA_PORT_A, 0b10010000);  // Tone 0 volume = 0
        for (int i = 0; i < 10; ++i) machine.step();

        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, false);
        for (int i = 0; i < 20; ++i) machine.step();

        set_latch_bit(machine, LATCH_ADDR_SOUND_WRITE, true);
        for (int i = 0; i < 10; ++i) machine.step();

        tone0 = machine.memory().sound_chip.get_tone_channel_state(0);
        REQUIRE(tone0.volume == 0);

        // Run to generate samples
        for (int i = 0; i < 100'000; ++i) {
            machine.step();
        }

        // Verify audio output
        REQUIRE(machine.memory().audio_buffer.has_value());
        AudioBuffer& audio = machine.memory().audio_buffer.value();

        std::vector<AudioSample> samples(500);
        size_t count = audio.read(samples.data(), 500);
        REQUIRE(count > 0);
        REQUIRE(has_channel_activity(samples, 0));
    }
}

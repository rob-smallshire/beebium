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

#include "beebium/devices/Sn76489.hpp"
#include "beebium/AudioBuffer.hpp"
#include <cassert>

namespace beebium {

Sn76489::Sn76489(uint32_t clock_hz, uint32_t sample_rate)
    : tone_{}
    , noise_{}
    , latched_reg_(0)
    , phase_accumulator_(0)
    , phase_increment_((1ULL << 32) / 8)  // Emulator: 2 MHz call rate ÷8 → 250 kHz internal
    , sample_accumulator_(0)
    , clock_hz_(clock_hz)
    , sample_rate_(sample_rate)
{
    reset();
}

void Sn76489::reset() {
    // Initialize all channels to silent
    for (auto& tone : tone_) {
        tone.frequency = 0;
        tone.counter = 0;
        tone.output_bit = false;
        tone.volume = 15;  // Silent
    }

    noise_.rate_select = 0;
    noise_.white_mode = true;
    noise_.lfsr = LFSR_INIT;
    noise_.volume = 15;  // Silent
    noise_.counter = NOISE_RATE_DIVIDERS[0];
    noise_.output_bit = false;

    latched_reg_ = 0;
    phase_accumulator_ = 0;
    sample_accumulator_ = 0;
}

void Sn76489::write(uint8_t data) {
    // Record write trace for debugging
    if (write_trace_count_ < WRITE_TRACE_SIZE) {
        auto& entry = write_trace_[write_trace_count_++];
        entry.data = data;
        entry.is_latch = (data & 0x80) != 0;
        entry.reg_type = entry.is_latch ? ((data >> 4) & 0x07) : latched_reg_;
    }

    if (data & 0x80) {
        // Latch byte: %1cctdddd
        uint8_t channel_type = (data >> 4) & 0x07;  // Bits 6-4: channel/type select
        uint8_t data_low = data & 0x0F;             // Bits 3-0: data low nibble

        latched_reg_ = channel_type;

        switch (channel_type) {
            case 0: // Tone 0 frequency
                tone_[0].frequency = (tone_[0].frequency & 0x3F0) | data_low;
                break;
            case 1: // Tone 0 volume
                tone_[0].volume = data_low;
                break;
            case 2: // Tone 1 frequency
                tone_[1].frequency = (tone_[1].frequency & 0x3F0) | data_low;
                break;
            case 3: // Tone 1 volume
                tone_[1].volume = data_low;
                break;
            case 4: // Tone 2 frequency
                tone_[2].frequency = (tone_[2].frequency & 0x3F0) | data_low;
                break;
            case 5: // Tone 2 volume
                tone_[2].volume = data_low;
                break;
            case 6: // Noise control
                noise_.rate_select = data_low & 0x03;      // Bits 1-0: rate
                noise_.white_mode = (data_low & 0x04) != 0; // Bit 2: white/periodic
                // Reset LFSR on any noise control write (MAME-verified behavior)
                noise_.lfsr = LFSR_INIT;
                // Reset noise counter
                if (noise_.rate_select < 3) {
                    noise_.counter = NOISE_RATE_DIVIDERS[noise_.rate_select];
                }
                break;
            case 7: // Noise volume
                noise_.volume = data_low;
                break;
        }
    } else {
        // Data byte: %0DDDDDD (upper 6 bits of tone frequency)
        uint8_t data_high = data & 0x3F;

        // Only tone frequency registers (0, 2, 4) accept data bytes
        switch (latched_reg_) {
            case 0: // Tone 0 frequency
                tone_[0].frequency = (tone_[0].frequency & 0x00F) | (data_high << 4);
                break;
            case 2: // Tone 1 frequency
                tone_[1].frequency = (tone_[1].frequency & 0x00F) | (data_high << 4);
                break;
            case 4: // Tone 2 frequency
                tone_[2].frequency = (tone_[2].frequency & 0x00F) | (data_high << 4);
                break;
            default:
                // Data bytes ignored for volume and noise registers
                break;
        }
    }
}

void Sn76489::tick(AudioBuffer& buffer) {
    // Phase accumulator: tick at 250 kHz from 2 MHz input
    phase_accumulator_ += phase_increment_;

    if (phase_accumulator_ >= (1ULL << 32)) {
        phase_accumulator_ -= (1ULL << 32);
        update_250khz_state();

        // Sample accumulator: generate 48 kHz output from 250 kHz internal
        sample_accumulator_ += sample_rate_;
        while (sample_accumulator_ >= (clock_hz_ / 16)) {  // 250000 Hz internal rate
            emit_sample(buffer);
            sample_accumulator_ -= (clock_hz_ / 16);
        }
    }
}

void Sn76489::update_250khz_state() {
    update_tone_channels();
    update_noise_channel();
}

void Sn76489::update_tone_channels() {
    for (size_t i = 0; i < 3; ++i) {
        auto& tone = tone_[i];

        // Decrement counter
        if (tone.counter > 0) {
            tone.counter--;
        } else {
            // Counter reached zero: toggle output and reload
            tone.output_bit = !tone.output_bit;

            // Reload counter from frequency register
            // N=0 is treated as N=1024 (very low frequency)
            tone.counter = (tone.frequency == 0) ? 1024 : tone.frequency;
        }
    }
}

void Sn76489::update_noise_channel() {
    // Decrement noise counter
    if (noise_.counter > 0) {
        noise_.counter--;
    } else {
        // Counter reached zero: update LFSR and output bit
        if (noise_.white_mode) {
            noise_.output_bit = next_white_noise_bit();
        } else {
            noise_.output_bit = next_periodic_noise_bit();
        }

        // Reload counter based on rate select
        if (noise_.rate_select < 3) {
            noise_.counter = NOISE_RATE_DIVIDERS[noise_.rate_select];
        } else {
            // Rate 3: use tone 2's period x 2 (MAME-verified behavior)
            // No synchronization to tone 2's counter state is needed
            uint16_t tone2_freq = (tone_[2].frequency == 0) ? 1024 : tone_[2].frequency;
            noise_.counter = tone2_freq * 2;
        }
    }
}

uint8_t Sn76489::next_white_noise_bit() {
    // 15-bit LFSR with taps at bits 0 and 1
    // Feedback polynomial: x^15 + x^14 + 1
    // Maximal-length sequence: period = 32767 (2^15 - 1)
    uint8_t feedback = ((noise_.lfsr >> 1) ^ noise_.lfsr) & 1;
    noise_.lfsr = (noise_.lfsr >> 1) | (feedback << 14);
    noise_.lfsr &= LFSR_MASK;  // Keep 15 bits
    return noise_.lfsr & 1;
}

uint8_t Sn76489::next_periodic_noise_bit() {
    // Periodic noise: simple 15-bit rotation
    // Produces a 15-cycle pattern
    uint8_t result = noise_.lfsr & 1;
    noise_.lfsr = ((noise_.lfsr >> 1) | (noise_.lfsr << 14)) & LFSR_MASK;
    return result;
}

void Sn76489::emit_sample(AudioBuffer& buffer) {
    // Generate audio sample with 4 separate channels
    // Uses unsigned encoding (0-255) with DC bias pre-applied
    AudioSample sample;

    // Get normalized samples for each channel (unsigned, centered at 128)
    uint8_t tone0 = get_tone_normalized(0);
    uint8_t tone1 = get_tone_normalized(1);
    uint8_t tone2 = get_tone_normalized(2);
    uint8_t noise_samp = get_noise_normalized();

    // Pack into 32-bit field: [tone0|tone1|tone2|noise]
    sample.pack_4x8bit_unsigned(0, tone0, tone1, tone2, noise_samp);

    // Sources 1-3 reserved for future expansion (speech, 1MHz bus audio)
    sample.sources[1] = 0;
    sample.sources[2] = 0;
    sample.sources[3] = 0;

    buffer.push(sample);
}

int8_t Sn76489::get_tone_amplitude(size_t channel) const {
    assert(channel < 3);
    const auto& tone = tone_[channel];

    // Volume 15 = silence
    if (tone.volume >= 15) {
        return 0;
    }

    // Get base amplitude from volume table
    int8_t amplitude = VOLUME_TABLE[tone.volume];

    // Apply square wave polarity
    return tone.output_bit ? amplitude : -amplitude;
}

int8_t Sn76489::get_noise_amplitude() const {
    // Volume 15 = silence
    if (noise_.volume >= 15) {
        return 0;
    }

    // Get base amplitude from volume table
    int8_t amplitude = VOLUME_TABLE[noise_.volume];

    // Apply noise bit polarity
    return noise_.output_bit ? amplitude : -amplitude;
}

uint8_t Sn76489::get_tone_normalized(size_t channel) const {
    assert(channel < 3);
    const auto& tone = tone_[channel];
    return normalized_level(tone.volume, tone.output_bit);
}

uint8_t Sn76489::get_noise_normalized() const {
    return normalized_level(noise_.volume, noise_.output_bit);
}

uint8_t Sn76489::normalized_level(uint8_t volume, bool output_bit) {
    // Unipolar output, matching the real SN76489: the pin sits at a silence
    // level and pulls to a louder level while the flip-flop is high, so the
    // short-term MEAN of a fast (ultrasonic-period) channel tracks the volume
    // register. This is what lets sampled-sound players recover PCM by
    // modulating volume; a symmetric bipolar square would leave the mean fixed
    // and carry no baseband.
    //
    // Silence is level 0; the high level is twice the volume-table amplitude, so
    // the AC swing about the mean (amplitude, up to 127) is unchanged from the
    // previous bipolar encoding and ordinary audio keeps its loudness. Volume 15
    // is the silence level, not the mid-point.
    if (volume >= 15) {
        return 0;
    }
    int amplitude = VOLUME_TABLE[volume];  // 0-127
    return static_cast<uint8_t>(output_bit ? (2 * amplitude) : 0);
}

// --- Introspection interface ---

void Sn76489::compute_voltage_levels(uint8_t volume, float& dc_bias,
                                     float& peak, float& trough) {
    // The output is unipolar: it rests at the silence level when the flip-flop
    // is low and rises by the volume-dependent swing when it is high. The
    // MID-POINT therefore varies with volume (silence + swing/2), which is the
    // property sampled-sound playback exploits; a silent channel sits at the
    // silence level with no swing.
    if (volume >= 15) {
        trough = DC_BIAS_SILENT_V;
        peak = DC_BIAS_SILENT_V;
        dc_bias = DC_BIAS_SILENT_V;
    } else {
        float swing = VOLUME_SWING_V[volume];
        trough = DC_BIAS_SILENT_V;          // flip-flop low: silence level
        peak = DC_BIAS_SILENT_V + swing;    // flip-flop high: louder level
        dc_bias = DC_BIAS_SILENT_V + swing / 2.0f;  // mean tracks volume
    }
}

Sn76489::ToneChannelState Sn76489::get_tone_channel_state(size_t channel) const {
    assert(channel < 3);
    const auto& tone = tone_[channel];

    ToneChannelState state;
    state.frequency = tone.frequency;
    state.counter = tone.counter;
    state.output_bit = tone.output_bit;
    state.volume = tone.volume;

    // Compute output frequency: f_out = f_clock / (32 × N)
    uint16_t divider = (tone.frequency == 0) ? 1024 : tone.frequency;
    state.frequency_hz = static_cast<float>(clock_hz_) / (32.0f * divider);

    state.amplitude = get_tone_amplitude(channel);

    // Compute DC bias and voltage levels
    compute_voltage_levels(tone.volume, state.dc_bias_v, state.peak_v, state.trough_v);

    return state;
}

Sn76489::NoiseChannelState Sn76489::get_noise_channel_state() const {
    NoiseChannelState state;
    state.rate_select = noise_.rate_select;
    state.white_mode = noise_.white_mode;
    state.lfsr = noise_.lfsr;
    state.volume = noise_.volume;

    // Compute noise update rate
    if (noise_.rate_select < 3) {
        uint16_t divider = NOISE_RATE_DIVIDERS[noise_.rate_select];
        state.rate_hz = static_cast<float>(clock_hz_) / (16.0f * divider);  // 250 kHz / divider
    } else {
        // Clocked from tone 2
        uint16_t tone2_divider = (tone_[2].frequency == 0) ? 1024 : tone_[2].frequency;
        state.rate_hz = static_cast<float>(clock_hz_) / (32.0f * tone2_divider);
    }

    state.amplitude = get_noise_amplitude();

    // Compute DC bias and voltage levels
    compute_voltage_levels(noise_.volume, state.dc_bias_v, state.peak_v, state.trough_v);

    return state;
}

} // namespace beebium

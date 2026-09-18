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

#pragma once

#include <cstdint>
#include <cstddef>

namespace beebium {

// Forward declarations
struct AudioSample;
class AudioBuffer;

// SN76489 Programmable Sound Generator
//
// This chip provides 3 tone generators + 1 noise generator, mixed to single output.
// On the BBC Micro:
// - Input clock: 4 MHz
// - Internal clock: 250 kHz (4 MHz ÷ 16)
// - Interface: Write-only via System VIA Port A (gated by addressable latch bit 0)
//
// Register protocol:
// - Latch byte (bit 7 = 1): %1cctdddd  (cct = channel/type select, dddd = data low 4 bits)
// - Data byte  (bit 7 = 0): %0DDDDDD   (DDDDDD = data high 6 bits for tone registers)
//
// Registers:
//   0: Tone 0 frequency (10-bit)
//   1: Tone 0 volume (4-bit, inverted: 0=max, 15=silent)
//   2: Tone 1 frequency
//   3: Tone 1 volume
//   4: Tone 2 frequency
//   5: Tone 2 volume
//   6: Noise control (2-bit rate + mode)
//   7: Noise volume
//
// Frequency calculation: f_out = f_clock / (32 × N)
//   where N is 10-bit divider value (1-1023), N=0 treated as 1024
//
// Volume: 4-bit logarithmic attenuation, ~2 dB per step
//
class Sn76489 {
public:
    // Constructor: clock_hz = input clock rate (e.g., 4000000 for 4 MHz)
    //             sample_rate = output sample rate (e.g., 48000)
    explicit Sn76489(uint32_t clock_hz, uint32_t sample_rate);

    // Hardware write interface (called when addressable latch enables sound writes)
    void write(uint8_t data);

    // Emulator clock advance - called at 2 MHz (CPU clock rate)
    //
    // The real hardware receives 4 MHz from the Video ULA and divides by 16
    // internally to get 250 kHz. The emulator achieves the same 250 kHz by
    // being called at 2 MHz and using a phase accumulator to divide by 8.
    //
    // Generates samples at 48 kHz via sample accumulator.
    void tick(AudioBuffer& buffer);

    // Reset to power-on state
    void reset();

    // --- Introspection interface for debugging/gRPC ---

    struct ToneChannelState {
        uint16_t frequency;      // 10-bit divider value (0-1023)
        uint16_t counter;        // Current countdown value
        bool output_bit;         // Square wave polarity (0 or 1)
        uint8_t volume;          // 4-bit attenuation (0=max, 15=silent)
        float frequency_hz;      // Computed output frequency
        int8_t amplitude;        // Current 8-bit signed amplitude (-127 to +127)

        // DC bias metadata for accurate analog reconstruction
        // Reference: scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style
        float dc_bias_v;         // Mid-point voltage for current volume setting
        float peak_v;            // Voltage when output_bit=1
        float trough_v;          // Voltage when output_bit=0
    };

    struct NoiseChannelState {
        uint8_t rate_select;     // 2-bit rate: 0=N/512, 1=N/1024, 2=N/2048, 3=Tone2
        bool white_mode;         // true=white noise, false=periodic
        uint16_t lfsr;           // 15-bit LFSR state
        uint8_t volume;          // 4-bit attenuation
        float rate_hz;           // Computed noise update rate
        int8_t amplitude;        // Current 8-bit signed amplitude

        // DC bias metadata for accurate analog reconstruction
        float dc_bias_v;         // Mid-point voltage for current volume setting
        float peak_v;            // Voltage when output_bit=1
        float trough_v;          // Voltage when output_bit=0
    };

    ToneChannelState get_tone_channel_state(size_t channel) const;
    NoiseChannelState get_noise_channel_state() const;

    // Compute DC bias and voltage levels for a given volume setting
    // This is public to allow tests to verify the computation directly
    static void compute_voltage_levels(uint8_t volume, float& dc_bias,
                                       float& peak, float& trough);

    // Query current latched register
    uint8_t latched_register() const { return latched_reg_; }

    // Debug: trace recent writes to the chip
    static constexpr size_t WRITE_TRACE_SIZE = 64;
    struct WriteTraceEntry {
        uint8_t data;
        bool is_latch;     // true if this was a latch byte (bit 7 set)
        uint8_t reg_type;  // channel_type for latch bytes
    };
    const WriteTraceEntry* get_write_trace() const { return write_trace_; }
    size_t get_write_trace_count() const { return write_trace_count_; }
    void clear_write_trace() { write_trace_count_ = 0; }

private:
    // --- Internal state ---

    struct ToneChannel {
        uint16_t frequency;      // 10-bit divider value (0-1023)
        uint16_t counter;        // Down-counter, reloads from frequency
        bool output_bit;         // Square wave flip-flop
        uint8_t volume;          // 4-bit attenuation (0-15)
    };

    struct NoiseChannel {
        uint8_t rate_select;     // 2-bit rate select (0-3)
        bool white_mode;         // true=white, false=periodic
        uint16_t lfsr;           // 15-bit linear feedback shift register
        uint8_t volume;          // 4-bit attenuation (0-15)
        uint16_t counter;        // Down-counter for noise rate
        bool output_bit;         // Current noise output
    };

    ToneChannel tone_[3];        // 3 tone generators
    NoiseChannel noise_;         // Noise generator
    uint8_t latched_reg_;        // Last latched register (0-7)

    // --- Timing accumulators ---
    //
    // Real hardware: 4 MHz input clock ÷16 = 250 kHz internal
    // Emulator: tick() called at 2 MHz (CPU rate), phase accumulator ÷8 = 250 kHz

    uint64_t phase_accumulator_;
    uint64_t phase_increment_;   // (2^32) / 8 for emulator's 2MHz→250kHz

    // Sample accumulator for 48 kHz output from 250 kHz internal
    uint32_t sample_accumulator_;
    uint32_t clock_hz_;
    uint32_t sample_rate_;

    // --- Helper methods ---

    // Update chip state at 250 kHz rate
    void update_250khz_state();

    // Update tone channel counters and flip-flops
    void update_tone_channels();

    // Update noise channel counter and LFSR
    void update_noise_channel();

    // Generate white noise bit (15-bit LFSR with taps at bits 0,1)
    uint8_t next_white_noise_bit();

    // Generate periodic noise bit (15-cycle pattern)
    uint8_t next_periodic_noise_bit();

    // Emit audio sample to buffer (uses unsigned encoding with DC bias pre-applied)
    void emit_sample(AudioBuffer& buffer);

    // Compute tone channel output amplitude (applies volume table)
    // Returns signed -127 to +127 (legacy, for signed encoding)
    int8_t get_tone_amplitude(size_t channel) const;

    // Compute noise channel output amplitude
    // Returns signed -127 to +127 (legacy, for signed encoding)
    int8_t get_noise_amplitude() const;

    // Compute normalized tone channel sample (DC bias pre-applied)
    // Returns unsigned 0-255, centered at 128
    uint8_t get_tone_normalized(size_t channel) const;

    // Compute normalized noise channel sample (unipolar; see normalized_level)
    uint8_t get_noise_normalized() const;

    // Map a volume/output-bit pair to a unipolar 0-254 sample: 0 = silence,
    // 2*amplitude = flip-flop high. The mean of a fast channel tracks volume.
    static uint8_t normalized_level(uint8_t volume, bool output_bit);

    // Logarithmic volume table: 4-bit register → 8-bit signed amplitude
    // Formula: amplitude[v] = round(127 × 10^(-0.1v))
    // Volume 15 = silence (0), Volume 0 = maximum (127)
    static constexpr int8_t VOLUME_TABLE[16] = {
        127,  // 0:   0 dB  (max amplitude)
        101,  // 1:  -2 dB
         80,  // 2:  -4 dB
         64,  // 3:  -6 dB
         51,  // 4:  -8 dB
         40,  // 5: -10 dB
         32,  // 6: -12 dB
         25,  // 7: -14 dB
         20,  // 8: -16 dB
         16,  // 9: -18 dB
         13,  // 10: -20 dB
         10,  // 11: -22 dB
          8,  // 12: -24 dB
          6,  // 13: -26 dB
          5,  // 14: -28 dB
          0   // 15: silence
    };

    // Noise LFSR period constants
    static constexpr uint16_t LFSR_INIT = 0x4000;  // Initial LFSR state (bit 14 set)
    static constexpr uint16_t LFSR_MASK = 0x7FFF;  // 15-bit mask

    // Noise rate dividers (from 250 kHz internal clock)
    // Rate 0: 32 (7812.5 Hz), Rate 1: 64 (3906.25 Hz), Rate 2: 128 (1953.125 Hz)
    // Formula: 1 << (5 + rate) - matches MAME and B2
    static constexpr uint16_t NOISE_RATE_DIVIDERS[3] = {32, 64, 128};

    // DC bias voltage constants (from hardware measurements)
    // Reference: scarybeastsecurity.blogspot.com/2020/06/sampled-sound-1980s-style
    // The SN76489 output is centered around ~3V, not 0V. This matters for
    // sampled sound techniques that modulate volume to encode PCM data.
    static constexpr float DC_BIAS_SILENT_V = 0.8f;    // Silent channel voltage

    // Per-volume swing (peak-to-peak voltage) for each attenuation level
    // Derived from 2dB attenuation curve; may need refinement from hardware measurements
    // Formula: swing[v] = 0.8 * 10^(-0.1 * v) for v in [0,14], swing[15] = 0
    static constexpr float VOLUME_SWING_V[16] = {
        0.800f,  //  0: max volume, full swing
        0.635f,  //  1: -2dB
        0.505f,  //  2: -4dB
        0.401f,  //  3: -6dB
        0.318f,  //  4: -8dB
        0.253f,  //  5: -10dB
        0.201f,  //  6: -12dB
        0.160f,  //  7: -14dB
        0.127f,  //  8: -16dB
        0.101f,  //  9: -18dB
        0.080f,  // 10: -20dB
        0.064f,  // 11: -22dB
        0.050f,  // 12: -24dB
        0.040f,  // 13: -26dB
        0.032f,  // 14: -28dB
        0.000f   // 15: silent
    };

    // Debug write trace
    WriteTraceEntry write_trace_[WRITE_TRACE_SIZE] = {};
    size_t write_trace_count_ = 0;
};

} // namespace beebium

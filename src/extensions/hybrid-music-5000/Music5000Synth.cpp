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

#include "Music5000Synth.hpp"

#include <algorithm>

namespace beebium {

namespace {

// A side's frame total over six slots, as DAC current units, rounded half
// away from zero.
int32_t per_six_slots(int32_t total) {
    return total >= 0 ? (total + 3) / 6 : -((-total + 3) / 6);
}

}  // namespace

void Music5000Synth::reset() {
    // While CRST_n is low the main RAM is written with zero in every slot,
    // and the sequencer only addresses the normal bank (INDEX is cleared).
    // The alternate bank is untouched (1.5).
    std::fill(ram_.begin() + kNormalBank, ram_.begin() + kAlternateBank, uint8_t{0});
    index_ = false;
    // IC21 is not reset, but it is reloaded every channel time, and with the
    // control registers zero it holds no modulation by the time the reset
    // ends.
    pending_index_ = false;

    // Slot 6 still addresses a wave byte, which is written with zero too.
    // Once the registers are zero the wave select is 0 and the frequency
    // adds nothing, so each channel zeroes wave 0 at its phase-high index
    // (1.5).
    for (std::size_t k = 0; k < kChannelCount; ++k) {
        ram_[(phase_[k] >> 17) & 0x7F] = 0;
    }
}

bool Music5000Synth::advance_phase(std::size_t k) {
    const uint8_t freq_lo = reg(k, kFreqLoRow);
    const uint32_t freq = static_cast<uint32_t>(freq_lo)
                        | static_cast<uint32_t>(reg(k, kFreqMidRow)) << 8
                        | static_cast<uint32_t>(reg(k, kFreqHiRow)) << 16;

    // Freq lo bit 0 (latched by IC3b in slot 1) gates the phase off the
    // adder's B input, so each byte is written with freq + 0: the phase is
    // loaded with all 24 bits of the frequency, bit 0 included, and no byte
    // carries (3.2).
    if (freq_lo & 0x01) {
        phase_[k] = freq;
        return false;
    }

    // Otherwise all eight bits of freq lo reach the adder (3.2).
    const uint32_t sum = phase_[k] + freq;
    phase_[k] = sum & 0xFFFFFF;
    return (sum >> 24) != 0;
}

void Music5000Synth::render_frame(int32_t& left, int32_t& right) {
    // Each side accumulates current x slots; the six-slot average is taken
    // once per frame.
    int32_t left_total = 0;
    int32_t right_total = 0;

    for (std::size_t k = 0; k < kChannelCount; ++k) {
        const bool wrap = advance_phase(k);
        wrapped_[k] = wrap;

        // Slot 6: the wave address is the wave select's upper nibble and
        // bits 7-1 of the new phase-high byte (2).
        const uint8_t wave = reg(k, kWaveSelectRow) >> 4;
        const uint8_t sample = ram_[static_cast<std::size_t>(wave) << 7
                                    | ((phase_[k] >> 17) & 0x7F)];

        // Slot 7: on a wrap the amplitude register is written into phase RAM
        // byte 3, and the latch feeding the adder captures it, so a new
        // amplitude applies from the wrapping sample on (4).
        if (wrap) {
            stored_amplitude_[k] = reg(k, kAmplitudeRow);
        }

        const uint8_t control = reg(k, kControlRow);
        const bool positive = (sample & 0x80) != 0;

        // Modulation: control bit 5 AND (this sample wrapped OR is positive),
        // taken before any inversion, selects the alternate bank for the
        // counter value two on (IC21 latches it in the next channel time,
        // IC3a at that time's end). With the column order that is register
        // column r modulating column r+1, and columns 7 and 15 modulating
        // columns 0 and 8 in the next sample (5).
        const bool modulate = (control & 0x20) != 0 && (wrap || positive);
        index_ = pending_index_;
        pending_index_ = modulate;

        // Slot 0: an 8-bit add of the log sample (sign in bit 7, 1 is
        // positive) and the stored amplitude. The DAC registers load only if
        // the add flipped bit 7; otherwise the channel is silent (4).
        const uint8_t sum = static_cast<uint8_t>(sample + stored_amplitude_[k]);
        if (((sum ^ sample) & 0x80) == 0) {
            continue;
        }

        // The sign reaching the DAC is the sample's, inverted by control
        // bit 4 (6).
        const bool invert = (control & 0x10) != 0;
        const int32_t sign = (positive != invert) ? 1 : -1;
        const uint8_t code = sum & 0x7F;

        // The DAC holds the code for six slots, each steered left (encode
        // mode, half a step more current) or right (decode mode) by the
        // position counter (6, 7.1). The time-slicing within the channel
        // time is above the audio band after the output filter, so the model
        // keeps only each side's share.
        const int left_count = left_slots(control & 0x0F);
        left_total += sign * encode_current(code) * left_count;
        right_total += sign * decode_current(code) * (6 - left_count);
    }

    left = per_six_slots(left_total);
    right = per_six_slots(right_total);
}

}  // namespace beebium

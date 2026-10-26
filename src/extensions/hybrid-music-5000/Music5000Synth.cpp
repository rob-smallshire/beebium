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

void Music5000Synth::reset() {
    // While CRST_n is low the main RAM is written with zero in every slot,
    // and the sequencer only addresses the normal bank (INDEX is cleared).
    // The alternate bank is untouched (1.5).
    std::fill(ram_.begin() + kNormalBank, ram_.begin() + kAlternateBank, uint8_t{0});
    index_ = false;

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
    left = 0;
    right = 0;
    for (std::size_t k = 0; k < kChannelCount; ++k) {
        wrapped_[k] = advance_phase(k);
    }
}

}  // namespace beebium

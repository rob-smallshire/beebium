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

#ifndef BEEBIUM_MUSIC5000_SYNTH_HPP
#define BEEBIUM_MUSIC5000_SYNTH_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace beebium {

// The digital signal processor of the Hybrid Music 5000 (and Music 3000).
//
// A model of the board's TTL as docs/music5000/hardware-model.md describes it,
// computed one 46875 Hz sample at a time. It has no framework dependencies;
// the hybrid-music-5000 extension wraps it for the 1 MHz bus and the audio
// frames. Section references below are to hardware-model.md.
class Music5000Synth {
public:
    static constexpr std::size_t kRamSize = 2048;
    static constexpr std::size_t kChannelCount = 16;

    // Register rows within a bank: the row address is base + row + column.
    static constexpr uint16_t kNormalBank = 0x700;
    static constexpr uint16_t kAlternateBank = 0x780;
    static constexpr uint16_t kFreqLoRow = 0x00;
    static constexpr uint16_t kFreqMidRow = 0x10;
    static constexpr uint16_t kFreqHiRow = 0x20;
    static constexpr uint16_t kWaveSelectRow = 0x50;
    static constexpr uint16_t kAmplitudeRow = 0x60;
    static constexpr uint16_t kControlRow = 0x70;

    Music5000Synth() = default;

    // A host write to JIM, as the bus delivers it: extended page and offset
    // within page 0xFD. Page bits 3-1 select the RAM block; bit 0 and the
    // upper nibble are not decoded here (1.1, 1.2). The real board lands the
    // write within one channel time; the model applies it at once.
    void write(uint8_t page, uint8_t offset, uint8_t value) {
        ram_[static_cast<std::size_t>(((page >> 1) & 0x07) << 8) | offset] = value;
    }

    // The bus's NRST line, asserted on power-up and BREAK (1.5). The model
    // gives both causes the same effect: zero the normal register bank and
    // INDEX.
    void reset();

    // The register column read by channel counter value k (0-15). The DSP
    // visits the columns in the order 0, 8, 1, 9, ..., 7, 15 (1.3).
    static constexpr std::size_t column_for_counter(std::size_t k) {
        return ((k & 1) << 3) | (k >> 1);
    }

    // RAM address of a register (row, column) in a bank.
    static constexpr uint16_t register_address(uint16_t bank, uint16_t row, std::size_t column) {
        return static_cast<uint16_t>(bank + row + column);
    }

    // Inspection.
    std::span<const uint8_t, kRamSize> ram() const { return ram_; }
    bool index() const { return index_; }

private:
    std::array<uint8_t, kRamSize> ram_{};

    // The INDEX flip-flop (IC3a): selects the alternate bank for every
    // register read of a channel (5).
    bool index_ = false;
};

}  // namespace beebium

#endif  // BEEBIUM_MUSIC5000_SYNTH_HPP

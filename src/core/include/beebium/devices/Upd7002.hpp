// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/ClockTypes.hpp>

#include <array>
#include <cstdint>

namespace beebium {

// The conversion time of the uPD7002, in 1 MHz ADC clocks from the start
// write to completion, as a function of the mode and of the 12-bit code
// (0 to 4095) the conversion produces. The datasheet gives only ranges
// (2.4 to 5 ms in 8-bit mode, 8.5 to 15 ms in 10-bit mode at 1 MHz); these
// constants reproduce the shape measured on real Model Bs, and are the one
// place to re-fit from new measurements (trial 7 of
// docs/discussion/analogue-port-hardware-trials.md).
namespace upd7002_timing {

// 8-bit mode: one straight line, t = 2950 + code / 2. Fitted to the 8-bit
// plot of conversion time against result posted on Stardot (Beebium thread,
// p=492873): 2.95 ms at code 0 rising 0.5 us per 12-bit code to 5.0 ms.
inline constexpr uint32_t EIGHT_BIT_START_CLOCKS = 2950;
inline constexpr uint32_t EIGHT_BIT_CODES_PER_CLOCK = 2;

// 12-bit (10-bit accurate) mode: a sawtooth of seven teeth. Within a tooth
// the time rises 2 clocks per code from the tooth's start time; each later
// tooth starts about 1 ms lower than the previous one ends. Boundary codes
// and start times from the same Stardot plot, digitised to about +/- 30 us
// (survey section 2.2), and consistent with the Stardot t=27458 timings on
// two other Model Bs. Per-chip boundary shifts, the overlap band past each
// boundary, jitter and noise are not modelled.
inline constexpr uint32_t TWELVE_BIT_CLOCKS_PER_CODE = 2;

struct Tooth {
    uint16_t first_code;   // the boundary code b_k
    uint32_t start_clocks; // T_k, the time at b_k
};

inline constexpr std::array<Tooth, 7> TWELVE_BIT_TEETH = {{
    {0, 9100},
    {1020, 10118},
    {1452, 9957},
    {2052, 10139},
    {2628, 10265},
    {3204, 10387},
    {3796, 10553},
}};

// Clocks from the start write to completion for a conversion producing
// code12 in the given mode.
constexpr uint32_t conversion_clocks(bool twelve_bit, uint16_t code12) {
    if (!twelve_bit) {
        return EIGHT_BIT_START_CLOCKS + code12 / EIGHT_BIT_CODES_PER_CLOCK;
    }
    const Tooth* tooth = &TWELVE_BIT_TEETH[0];
    for (const Tooth& candidate : TWELVE_BIT_TEETH) {
        if (candidate.first_code <= code12) {
            tooth = &candidate;
        }
    }
    return tooth->start_clocks
         + TWELVE_BIT_CLOCKS_PER_CODE * (code12 - tooth->first_code);
}

}  // namespace upd7002_timing

// NEC uPD7002 four-channel analogue-to-digital converter, as fitted to the
// BBC Micro at &FEC0-&FEDF (docs/discussion/upd7002-adc-specification.md).
//
// Registers, by offset (address & 3):
//   0  read: status; write: latch channel, mode and flag, start a conversion
//   1  read: result high byte, which resets EOC
//   2  read: result low byte (bits 3 to 0 read 0)
//   3  read: the low byte again; write: test mode, modelled as ignored
//
// Status: bit 7 EOC as the pin (1 = not complete), bit 6 not busy, bits 5
// and 4 the result's two most significant bits (the previous result's while
// a conversion runs), bit 3 mode, bit 2 flag, bits 1 and 0 channel.
//
// The chip has no reset pin: Break does not reach it. It is clocked at
// 1 MHz and counts its conversion time in those clocks. EOC is a level,
// read by the System VIA as CB1.
class Upd7002 {
public:
    static constexpr ClockEdge clock_edges = ClockEdge::Falling;
    static constexpr ClockRate clock_rate = ClockRate::Rate_1MHz;

    static constexpr int CHANNEL_COUNT = 4;

    // An input with nothing driving it: the centre of the range.
    static constexpr uint16_t UNDRIVEN_LEVEL = 0x8000;

    Upd7002() { power_on(); }

    // The state the chip has after power is applied: idle, EOC inactive,
    // not busy, control bits clear, a zero result, CB1 high. Input levels
    // belong to whatever drives the inputs and are kept.
    void power_on();

    uint8_t read(uint16_t offset);
    void write(uint16_t offset, uint8_t value);
    uint8_t peek(uint16_t offset) const;

    // One 1 MHz clock.
    void tick_falling();

    // The level on an input, as a fraction of VREF: 0 is analogue ground,
    // &FFFF is VREF. The 12-bit code is level >> 4.
    void set_input_level(int channel, uint16_t level);
    uint16_t input_level(int channel) const;

    // The EOC pin as System VIA CB1 sees it: 1 while idle or converting, 0
    // from completion until the high byte is read or a conversion starts.
    uint8_t eoc_level() const { return eoc_ ? 0 : 1; }

    // Inspection.
    uint8_t status() const;
    uint16_t result() const { return result_; }
    int channel() const { return control_ & 0x03; }
    bool twelve_bit() const { return (control_ & CONTROL_12_BIT) != 0; }
    bool flag() const { return (control_ & CONTROL_FLAG) != 0; }
    bool busy() const { return clocks_remaining_ != 0; }
    uint32_t clocks_remaining() const { return clocks_remaining_; }

private:
    static constexpr uint8_t CONTROL_MASK = 0x0F;
    static constexpr uint8_t CONTROL_12_BIT = 0x08;
    static constexpr uint8_t CONTROL_FLAG = 0x04;

    void start_conversion(uint8_t value);
    void complete_conversion();

    std::array<uint16_t, CHANNEL_COUNT> input_levels_ = {
        UNDRIVEN_LEVEL, UNDRIVEN_LEVEL, UNDRIVEN_LEVEL, UNDRIVEN_LEVEL};
    uint8_t control_ = 0;           // bits 3 to 0 as last written
    uint16_t result_ = 0;           // the result the registers show
    uint16_t pending_result_ = 0;   // what the conversion in flight produces
    uint32_t clocks_remaining_ = 0; // 0 when idle
    bool eoc_ = false;              // true from completion until reset
};

}  // namespace beebium

// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// AesTiming.hpp, the board timing interface
// version 2.0.0, 20 November 2026.

// AesTiming, the board's clock: what each cycle the ARM makes costs, counted in ticks of the
// 20 MHz crystal. See AesTiming.cpp for the units.

#pragma once

#include "AesOptions.hpp"

#include <cstdint>

// The cycle times in ticks, shared with AesTube, which prices instructions before running them.
extern const int kS, kN, kPalClock, kHostCycle;

// The clock. AesMemory calls memory_cycle or internal_cycle for every cycle; AesTube, the
// debugger and the Tube log read ticks().
class AesTiming {
public:
    // Which device a cycle reached, as AesMemory decides it: each has its own cost.
    enum class Place { Ram, Rom, Tube, Nothing };

    // A clock at 0, priced by the switches in `options`.
    explicit AesTiming(const AesOptions& options);

    // Called by AesMemory for every cycle the ARM makes: a memory cycle, or an internal cycle.
    void memory_cycle(bool seq, Place place, uint32_t addr);
    void internal_cycle(Place place, uint32_t addr);
    // The total ticks used since power-on.
    uint64_t ticks() const { return ticks_; }

private:
    // Adds a cycle's cost, then a refresh if one is due and may start.
    void pass(int cost, bool refresh_can_start);

    const AesOptions opt_;
    uint64_t ticks_ = 0;
    uint64_t next_refresh_;  // the tick at which the next refresh request is due
    uint32_t last_addr_ = 0;  // the address of the cycle before, memory or internal
};

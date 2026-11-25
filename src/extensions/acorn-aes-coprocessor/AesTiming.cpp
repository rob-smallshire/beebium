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
// AesTiming.cpp, cycle costs and refresh, in crystal ticks
// version 2.0.0, 20 November 2026.

// Board time. AesTiming is the board's clock: AesMemory passes it every memory cycle and every
// internal cycle the ARM makes, and it adds each one's cost to a running total of crystal ticks,
// ticks_. AesTube compares that total with the host's time to keep the ARM in step with the BBC,
// the debugger shows it as the cycle count, and the Tube log stamps its rows with it. So every
// cycle must come through here, and its cost must include the two things that can stretch it:
// the RAM's refresh, and the wait states of the ROM and the Tube.
//
// The units:
//   tick         one period of the 20 MHz crystal, 50 ns.
//   S cycle      a sequential memory cycle, to the word after the last one: 3 ticks.
//   N cycle      a non-sequential memory cycle: 6 ticks.
//   PAL clock    the clock of the board's logic chips: 3 ticks.
//   wait         PAL clocks a slower device adds to its cycle (time.rom_wait, time.tube_wait).
//   refresh      every time.refresh_period ticks the RAM asks for a refresh, which stops the ARM
//                for time.refresh_cost ticks.
//   block break  RAM works in blocks of 32 words; a sequential cycle that crosses into a new
//                block costs N, not S (time.block_break).
// The switches and their defaults are in AesOptions.hpp.
//
// For example, an LDR that reads a word from ROM: its data cycle is an N cycle (6 ticks) plus the
// ROM's wait (8 PAL clocks of 3 ticks by default, 24), so 30 ticks; and if a refresh is due, its
// cost on top.

#include "AesTiming.hpp"

// The cycle times in ticks: S, N, the PAL clock the waits are counted in, and one host cycle
// (the BBC's 2 MHz).
const int kS = 3, kN = 6;
const int kPalClock = 3;
const int kHostCycle = 10;
const uint32_t kBlockWordBits = 0x7C;  // Address bits A2-A6: the word's place in its block.

namespace {

// Whether a cycle went to RAM, the only place with S cycles and block breaks.
bool is_ram(AesTiming::Place place) {
    return place == AesTiming::Place::Ram;
}

}  // namespace

// The clock starts at 0, with the first refresh request one period away.
AesTiming::AesTiming(const AesOptions& options)
    : opt_(options), next_refresh_(options.time_refresh_period) {}

// Adds one cycle's cost to the clock, then runs a refresh if one is due and the cycle ended at a
// point where one may start. The ARM is stopped during a refresh, so its cost is simply added.
// Requests come at a steady beat, so a refresh that starts late does not delay the next request:
// next_refresh_ moves on by whole periods past the present.
void AesTiming::pass(int cost, bool refresh_can_start) {
    ticks_ += cost;
    if (refresh_can_start && ticks_ >= next_refresh_) {
        ticks_ += opt_.time_refresh_cost;
        while (next_refresh_ <= ticks_) next_refresh_ += opt_.time_refresh_period;
    }
}

// Costs one memory cycle, from whether it is sequential, which device it reached (`place`, as
// AesMemory decides it) and its address, which the block break needs.
void AesTiming::memory_cycle(bool seq, Place place, uint32_t addr) {
    // time.links set to SeqTied: no cycle counts as sequential.
    if (opt_.time_links == AesOptions::Links::SeqTied) seq = false;
    const bool ram = is_ram(place);
    // The block break, by time.block_break. AfterLastWord: a sequential cycle breaks when the
    // cycle before it, memory or internal, was at a block's last word. AtBlockStart: when this
    // cycle's own address starts a block. None: never. A refresh may start at the same point.
    const bool after_last = (last_addr_ & kBlockWordBits) == kBlockWordBits;
    const bool own_start = (addr & kBlockWordBits) == 0;
    const bool at_point = seq && (opt_.time_block_break == AesOptions::BlockBreak::AfterLastWord ? after_last : own_start);
    const bool breaks = at_point && opt_.time_block_break != AesOptions::BlockBreak::None;
    // Kept for the next cycle's block-break test.
    last_addr_ = addr;

    // Only a sequential RAM cycle that does not break costs S; everything else, ROM, Tube and
    // empty addresses included, costs N.
    int cost = (ram && seq && !breaks) ? kS : kN;
    // The ROM and the Tube add their waits, in PAL clocks.
    if (place == Place::Rom) cost += opt_.time_rom_wait * kPalClock;
    if (place == Place::Tube) cost += opt_.time_tube_wait * kPalClock;
    // Outside RAM, or on a non-sequential cycle, a refresh may always start; on a sequential RAM
    // cycle only at the block break's point.
    pass(cost, !ram || !seq || at_point);

    // time.tube_sync set to NextHostEdge: a Tube cycle also waits for the host's next 2 MHz edge.
    if (place == Place::Tube && opt_.time_tube_sync == AesOptions::TubeSync::NextHostEdge) {
        ticks_ += (kHostCycle - ticks_ % kHostCycle) % kHostCycle;
    }
}

// Costs one internal cycle, which uses no memory: the same as an S cycle. It counts as
// sequential, so in RAM it holds back a refresh unless its address is a block's last word; in
// ROM or the Tube a refresh may start at once.
void AesTiming::internal_cycle(Place place, uint32_t addr) {
    const bool seq = opt_.time_links != AesOptions::Links::SeqTied;
    const bool block_end = (addr & kBlockWordBits) == kBlockWordBits;
    last_addr_ = addr;
    pass(kS, !is_ram(place) || !seq || block_end);
}

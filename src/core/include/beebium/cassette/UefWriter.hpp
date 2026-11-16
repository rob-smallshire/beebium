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

#pragma once

#include "EdgeTrack.hpp"

#include <cstdint>
#include <vector>

namespace beebium {

// One UEF chunk: its id and body, as in the file.
struct UefChunk {
    uint16_t id = 0;
    std::vector<uint8_t> body;
};

// Describe a tape's edges as UEF tape chunks at the nominal 1200 Hz base, the
// way the UefTape reader renders them back. The content is preserved cycle for
// cycle: every half-cycle comes back in the same class (2400 or 1200 Hz) and
// every gap within one 1200 Hz half-cycle of its length. The chunk layout and
// the absolute timing are not: the machine's 16/13 MHz clock makes its tones
// 0.16% faster than the nominal base, as a real recording described by any UEF
// tool is.
//
// The edges are walked in regions separated by more than two 1200 Hz
// half-cycles (1024 clocks) of silence, and each region's half-cycles are
// classed short (2400 Hz) or long (1200 Hz) by the nearer nominal length and
// paired into cycles. In order of preference a region becomes:
//
//   &0110  carrier: runs of short cycles, at most 65535 cycles a chunk
//   &0100  bytes framing cleanly as 8N1, where the level before the start bit
//          is mark (or the region begins), at 1200 baud (one long or two short
//          cycles a bit) or 300 (four long or eight short); &0117 precedes a
//          byte whose baud differs from the one in force (1200 at the start of
//          the file)
//   &0104  bytes framing cleanly as 8E1, 8O1, 7E1 or 7O1 instead
//   &0102  explicit 1200-baud bits for whatever does not frame
//
// A run of bytes keeps the format and baud it started in while they frame;
// where it starts, the format and baud whose run of frames covers the most
// cycles is taken, the baud in force and the order above breaking ties (a lone
// 300 baud byte also frames at 1200, as a start bit and three 0s, but its run
// is shorter). A 7E1 or 7O1 frame is as long as an 8N1 frame, its parity bit
// where bit 7 is, so it is written as the 8N1 byte it is cycle for cycle.
//
// The silences become &0112 (a whole number of 1200 Hz half-cycles, within the
// two clocks the reader's rounding allows) or &0116 (seconds). A half-cycle
// without a partner of its class (the first or last at a recording's edge) is
// absorbed into the silence beside it. The last chunk is the silence that keeps
// the tape its length.
std::vector<UefChunk> classify_tape_edges(const EdgeTrack& track);

// A whole UEF file: the header (format 0.10), the metadata chunks, then the tape
// chunks; gzipped if asked.
std::vector<uint8_t> encode_uef(const std::vector<UefChunk>& metadata, const std::vector<UefChunk>& tape,
                                bool gzip);

}  // namespace beebium

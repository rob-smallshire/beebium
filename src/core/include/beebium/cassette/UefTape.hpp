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

#include "TapeSignal.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace beebium {

// A UEF tape image (format 0.10, document draft 28), rendered as the edges of
// ideal square cycles.
//
// The whole file is read into memory and its chunk list parsed at open; the
// tape chunks are rendered one at a time on demand. A cycle is a low half then
// a high half of equal length at the current base frequency (1200 Hz, carrier
// twice that); &0115 sets the phase, applied from the first cycle after a gap
// (default 180 degrees, so a cycle begins with its high half). An edge falls at
// every half-cycle boundary, and tape time is accumulated exactly at 16/13 MHz,
// so the same tape as a 48 kHz CSW gives the same edge times within one clock.
//
// Chunks rendered: &0100, &0104, &0110, &0111, &0112, &0113, &0114, &0115,
// &0116, &0117. Kept as metadata: &0000, &0001, &0005, &0009, &0120, &0130,
// &0131. Ignored: &0003, &0006 to &0008, &000A, &0101, &0103, &0105. Refused:
// &0200 and above (discs, ROMs, machine state). Any other chunk is skipped
// with one log line.
class UefTape final : public TapeSignal {
public:
    // Parse a UEF, plain or gzipped. Throws std::runtime_error with a sentence
    // naming `name` if the image cannot be read.
    static std::unique_ptr<UefTape> open(std::span<const uint8_t> bytes, const std::string& name);

    TapeItem next() override;
    void seek(TapeTime time) override;
    std::optional<TapeTime> duration() const override;
    TapeKind kind() const override { return TapeKind::Uef; }
    const TapeImageMetadata& metadata() const override { return metadata_; }

    // The render state at a chunk boundary. Public for the renderer only.
    struct RenderState {
        double exact_time = 0.0;     // tape time in fractional ULA clocks
        double base_hz = 1200.0;
        bool encoding_300 = false;
        uint16_t phase_degrees = 180;
        bool after_gap = true;       // the next cycle starts at the phase's level
        bool level = false;          // the level after the last edge
    };

    struct Chunk {
        uint16_t id = 0;
        std::vector<uint8_t> body;
    };

private:
    UefTape() = default;

    void render(size_t index, RenderState& state, std::vector<TapeItem>* out) const;

    std::string name_;
    TapeImageMetadata metadata_;
    bool makeuef_parity_swap_ = false;
    std::vector<Chunk> chunks_;              // tape chunks, in order
    std::vector<RenderState> chunk_starts_;  // state at the start of each
    RenderState end_state_;

    size_t next_chunk_ = 0;
    std::vector<TapeItem> buffer_;
    size_t buffer_pos_ = 0;
};

}  // namespace beebium

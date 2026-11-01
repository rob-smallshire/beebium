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

#ifndef BEEBIUM_LINUX_AUDIO_WIRE_DROPS_HPP
#define BEEBIUM_LINUX_AUDIO_WIRE_DROPS_HPP

#include <cstdint>

namespace beebium::linux_client {

// Samples the server produced but never sent, counted exactly from the
// chunks' sequence numbers (client specification 8.3): a chunk's sequence
// is the produced index of its first sample, so one that starts beyond the
// previous chunk's end follows a gap. The first chunk sets the baseline,
// since a subscription joins the stream wherever it is.
class WireDrops {
public:
    void chunk(std::uint64_t sequence, std::uint64_t sample_count) {
        if (started_ && sequence > expected_) {
            dropped_ += sequence - expected_;
        }
        started_ = true;
        expected_ = sequence + sample_count;
    }
    std::uint64_t dropped() const { return dropped_; }

private:
    bool started_ = false;
    std::uint64_t expected_ = 0;
    std::uint64_t dropped_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_AUDIO_WIRE_DROPS_HPP

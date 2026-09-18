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

// Test helpers to unpack the SN76489's 2x16 audio encoding. The chip occupies
// two source fields, each two signed 16-bit channels stored by pack_2x16bit as
// (low half = first channel, high half = second):
//   source 0 = (tone0, tone1)
//   source 1 = (tone2, noise)
// Silence is 0; a channel's full-scale high level is Sn76489::FULL_SCALE.

#pragma once

#include "beebium/AudioBuffer.hpp"

#include <cstdint>

namespace beebium {

inline int16_t sn_tone0(const AudioSample& s) {
    return static_cast<int16_t>(s.sources[0] & 0xFFFF);
}
inline int16_t sn_tone1(const AudioSample& s) {
    return static_cast<int16_t>((s.sources[0] >> 16) & 0xFFFF);
}
inline int16_t sn_tone2(const AudioSample& s) {
    return static_cast<int16_t>(s.sources[1] & 0xFFFF);
}
inline int16_t sn_noise(const AudioSample& s) {
    return static_cast<int16_t>((s.sources[1] >> 16) & 0xFFFF);
}

}  // namespace beebium

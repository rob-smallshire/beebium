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
}

}  // namespace beebium

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

#include "audio/PreRoll.hpp"

#include <algorithm>

namespace beebium::linux_client {

PreRoll::PreRoll(int sample_rate)
    : sample_rate_(sample_rate),
      initial_(frames(INITIAL_TARGET_MS, sample_rate)),
      step_(frames(TARGET_STEP_MS, sample_rate)),
      max_(frames(MAX_TARGET_MS, sample_rate)),
      target_(initial_) {}

std::size_t PreRoll::frames_to_pull(std::size_t available, std::size_t requested) {
    if (priming_) {
        if (available < target_) {
            return 0;
        }
        priming_ = false;
    }
    return requested;
}

void PreRoll::note_underrun() {
    target_ = std::min(max_, target_ + step_);
    priming_ = true;
}

void PreRoll::reset() {
    target_ = initial_;
    priming_ = true;
}

int PreRoll::target_milliseconds() const {
    return static_cast<int>(target_ * 1000 / static_cast<std::size_t>(sample_rate_));
}

}  // namespace beebium::linux_client

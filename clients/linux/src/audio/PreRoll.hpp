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

#ifndef BEEBIUM_LINUX_AUDIO_PRE_ROLL_HPP
#define BEEBIUM_LINUX_AUDIO_PRE_ROLL_HPP

#include <cstddef>

namespace beebium::linux_client {

// The adaptive pre-roll on the playback side of the ring, the macOS client's
// policy from #126 (client specification 8.3). Playback holds a target fill,
// the latency, starting at 120 ms. A read that comes back short raises the
// target by 60 ms, up to 480 ms, held for the session with no decay (a decay
// would drift back to the level that underran, and oscillate); playback then
// re-primes, silent until the ring refills to the new target, so a stall
// costs one gap and then stable output. A new connection resets it.
//
// Playback thread only.
class PreRoll {
public:
    static constexpr int CAPACITY_MS = 500;
    static constexpr int INITIAL_TARGET_MS = 120;
    static constexpr int TARGET_STEP_MS = 60;
    static constexpr int MAX_TARGET_MS = 480;

    static std::size_t frames(int milliseconds, int sample_rate) {
        return static_cast<std::size_t>(milliseconds) * static_cast<std::size_t>(sample_rate) / 1000;
    }

    explicit PreRoll(int sample_rate);

    // How many frames to take from a ring holding `available` for a callback
    // wanting `requested`: none while priming, else all of them. A read of
    // that many which comes back short must be reported to note_underrun().
    std::size_t frames_to_pull(std::size_t available, std::size_t requested);
    void note_underrun();
    void reset();

    std::size_t target_frames() const { return target_; }
    int target_milliseconds() const;
    bool priming() const { return priming_; }

private:
    int sample_rate_;
    std::size_t initial_;
    std::size_t step_;
    std::size_t max_;
    std::size_t target_;
    bool priming_ = true;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_AUDIO_PRE_ROLL_HPP

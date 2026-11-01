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

#ifndef BEEBIUM_LINUX_AUDIO_MIXER_CONTROLS_HPP
#define BEEBIUM_LINUX_AUDIO_MIXER_CONTROLS_HPP

#include "audio/AudioLayout.hpp"

#include <array>
#include <atomic>

namespace beebium::linux_client {

// The mixer's settings (client specification 8.2), written by the GUI and
// read by the playback thread once per callback, through atomics alone: the
// audio thread never takes a lock. Slider values are linear, 0 to 1; the
// renderer applies the volume curve. Groups and channels are indexed as in
// the AudioLayout; all start at unity and unmuted, so the mix is
// bit-identical until a control moves. They belong to the window, so they
// survive a reconnect; like the macOS client's, they are not persisted.
struct MixerControls {
    std::atomic<float> master{1.0f};
    std::atomic<bool> muted{false};
    std::array<std::atomic<float>, AudioLayout::MAX_GROUPS> group_volume{};
    std::array<std::atomic<bool>, AudioLayout::MAX_GROUPS> group_muted{};
    std::array<std::atomic<float>, AudioLayout::MAX_CHANNELS> channel_volume{};
    // -1 left, 0 centre, 1 right; no control shows it, as on macOS.
    std::array<std::atomic<float>, AudioLayout::MAX_CHANNELS> channel_pan{};

    MixerControls() {
        for (auto& volume : group_volume) {
            volume.store(1.0f);
        }
        for (auto& volume : channel_volume) {
            volume.store(1.0f);
        }
    }
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_AUDIO_MIXER_CONTROLS_HPP

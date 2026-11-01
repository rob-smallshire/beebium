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

#ifndef BEEBIUM_LINUX_AUDIO_AUDIO_RENDERER_HPP
#define BEEBIUM_LINUX_AUDIO_AUDIO_RENDERER_HPP

#include "audio/AudioLayout.hpp"
#include "audio/FrameRing.hpp"
#include "audio/MixerControls.hpp"
#include "audio/PreRoll.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace beebium::linux_client {

// The playback side of the audio path (client specification 8.1 to 8.3),
// the macOS client's AudioRenderer in its sound: each unipolar channel
// normalised by half full scale, DC-removed by a 10 Hz Butterworth
// high-pass, scaled by its volume, the mix gain and its group's sub-master,
// and panned at constant power; each bipolar stereo field summed as it is
// at the music gain and its group's sub-master; then the master volume and
// a soft limiter. No client low-pass: the server band-limits the chip.
//
// Reads the ring through the pre-roll. Playback thread only, apart from the
// controls, which are atomics.
class AudioRenderer {
public:
    static constexpr float HALF_FULL_SCALE = 8192.0f;   // Sn76489::FULL_SCALE / 2
    static constexpr float HIGHPASS_CUTOFF_HZ = 10.0f;
    // A quarter, the chip's weighting of one of four channels, over 1.18,
    // the high-pass overshoot on its lowest tone (the macOS client's
    // derivation; tools/audio-analysis/client_chain.py models it).
    static constexpr float MIX_GAIN = 0.2125f;
    static constexpr float MUSIC_FULL_SCALE = 32768.0f;
    // Four times MIX_GAIN: a Music 5000's digital full scale meets the
    // SN76489's four channels at full volume.
    static constexpr float MUSIC_MIX_GAIN = 0.85f;
    static constexpr std::size_t BLOCK_FRAMES = 1024;

    AudioRenderer(AudioLayout layout, FrameRing& ring, const MixerControls& controls);

    // Fill `frames` interleaved stereo frames, silence where there is no
    // sound to play. Always fills all of them.
    void render(float* interleaved, std::size_t frames);

    // The slider-to-gain curve: 40 dB from 0 to 1, and 0 at 0.
    static float volume_curve(float linear);
    // Identity below 0.8, an exponential shoulder towards 1 above: C1 at the
    // knee, odd, monotonic, strictly within (-1, 1).
    static float soft_limit(float x);

    const PreRoll& pre_roll() const { return pre_roll_; }
    // The pre-roll's target, readable from any thread: the playback latency.
    int latency_milliseconds() const { return latency_ms_.load(std::memory_order_relaxed); }
    const AudioLayout& layout() const { return layout_; }

private:
    struct Biquad {
        float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process(float x);
    };

    void render_block(float* interleaved, std::size_t frames);

    AudioLayout layout_;
    FrameRing& ring_;
    const MixerControls& controls_;
    PreRoll pre_roll_;
    std::vector<Biquad> highpass_;
    std::vector<std::uint32_t> scratch_;
    std::atomic<int> latency_ms_{PreRoll::INITIAL_TARGET_MS};
    bool started_ = false;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_AUDIO_AUDIO_RENDERER_HPP

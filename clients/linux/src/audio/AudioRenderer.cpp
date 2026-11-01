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

#include "audio/AudioRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beebium::linux_client {

namespace {

constexpr float PI = std::numbers::pi_v<float>;

std::int16_t half_of(std::uint32_t field, int half) {
    return static_cast<std::int16_t>(half == 0 ? field & 0xFFFFu : field >> 16);
}

}  // namespace

float AudioRenderer::Biquad::process(float x) {
    const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

AudioRenderer::AudioRenderer(AudioLayout layout, FrameRing& ring, const MixerControls& controls)
    : layout_(std::move(layout)),
      ring_(ring),
      controls_(controls),
      pre_roll_(static_cast<int>(layout_.sample_rate)),
      highpass_(layout_.channels.size()),
      scratch_(BLOCK_FRAMES * ring.fields(), 0) {
    // A second-order Butterworth high-pass (Q = 1/sqrt 2), the Audio EQ
    // Cookbook's coefficients, normalised by a0.
    const float omega = 2.0f * PI * HIGHPASS_CUTOFF_HZ / static_cast<float>(layout_.sample_rate);
    const float sn = std::sin(omega);
    const float cs = std::cos(omega);
    const float alpha = sn / (2.0f * (1.0f / std::sqrt(2.0f)));
    const float a0 = 1.0f + alpha;
    for (auto& filter : highpass_) {
        filter.b0 = ((1.0f + cs) / 2.0f) / a0;
        filter.b1 = (-(1.0f + cs)) / a0;
        filter.b2 = ((1.0f + cs) / 2.0f) / a0;
        filter.a1 = (-2.0f * cs) / a0;
        filter.a2 = (1.0f - alpha) / a0;
    }
}

float AudioRenderer::volume_curve(float linear) {
    if (linear <= 0.0f) {
        return 0.0f;
    }
    if (linear >= 1.0f) {
        return 1.0f;
    }
    return std::pow(10.0f, (linear - 1.0f) * 2.0f);
}

float AudioRenderer::soft_limit(float x) {
    constexpr float knee = 0.8f;
    const float magnitude = std::fabs(x);
    if (magnitude <= knee) {
        return x;
    }
    const float shoulder = (1.0f - knee) * (1.0f - std::exp(-(magnitude - knee) / (1.0f - knee)));
    return (x < 0.0f ? -1.0f : 1.0f) * (knee + shoulder);
}

void AudioRenderer::render(float* interleaved, std::size_t frames) {
    while (frames > 0) {
        const std::size_t block = std::min(frames, BLOCK_FRAMES);
        render_block(interleaved, block);
        interleaved += 2 * block;
        frames -= block;
    }
}

void AudioRenderer::render_block(float* interleaved, std::size_t frames) {
    // Through the pre-roll: nothing while priming, so the ring refills to
    // the target; a short read is an underrun, which the ring counts and the
    // pre-roll answers by raising the target and priming again.
    const std::size_t pull = pre_roll_.frames_to_pull(ring_.available(), frames);
    const std::size_t got = pull > 0 ? ring_.read(scratch_.data(), pull) : 0;
    if (pull > 0 && got < pull) {
        pre_roll_.note_underrun();
        latency_ms_.store(pre_roll_.target_milliseconds(), std::memory_order_relaxed);
    }

    const bool muted = controls_.muted.load(std::memory_order_relaxed);
    const float master = volume_curve(controls_.master.load(std::memory_order_relaxed));
    std::array<float, AudioLayout::MAX_GROUPS> group_gain{};
    for (std::size_t g = 0; g < layout_.groups.size(); ++g) {
        group_gain[g] = controls_.group_muted[g].load(std::memory_order_relaxed)
                            ? 0.0f
                            : volume_curve(controls_.group_volume[g].load(std::memory_order_relaxed));
    }
    const std::size_t channel_count = layout_.channels.size();
    std::array<float, AudioLayout::MAX_CHANNELS> channel_gain{};
    std::array<float, AudioLayout::MAX_CHANNELS> left_gain{};
    std::array<float, AudioLayout::MAX_CHANNELS> right_gain{};
    for (std::size_t c = 0; c < channel_count; ++c) {
        channel_gain[c] = volume_curve(controls_.channel_volume[c].load(std::memory_order_relaxed)) *
                          MIX_GAIN * group_gain[layout_.channels[c].group];
        const float pan = controls_.channel_pan[c].load(std::memory_order_relaxed);
        left_gain[c] = std::cos((pan + 1.0f) * PI / 4.0f);
        right_gain[c] = std::sin((pan + 1.0f) * PI / 4.0f);
    }

    const std::size_t fields = ring_.fields();
    for (std::size_t i = 0; i < frames; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        if (i < got && !muted) {
            const std::uint32_t* frame = &scratch_[i * fields];
            for (std::size_t c = 0; c < channel_count; ++c) {
                const auto& channel = layout_.channels[c];
                float sample = static_cast<float>(half_of(frame[channel.field], channel.half)) /
                               HALF_FULL_SCALE;
                sample = highpass_[c].process(sample);
                sample *= channel_gain[c];
                left += sample * left_gain[c];
                right += sample * right_gain[c];
            }
            // Already AC, zero is silence: no DC removal (8.1).
            for (const auto& stereo : layout_.stereo_fields) {
                const std::uint32_t field = frame[stereo.field];
                const float gain = MUSIC_MIX_GAIN * group_gain[stereo.group] / MUSIC_FULL_SCALE;
                left += static_cast<float>(half_of(field, 0)) * gain;
                right += static_cast<float>(half_of(field, 1)) * gain;
            }
            left = soft_limit(left * master);
            right = soft_limit(right * master);
        }
        interleaved[2 * i] = left;
        interleaved[2 * i + 1] = right;
    }
}

}  // namespace beebium::linux_client

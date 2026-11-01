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

// The audio path without a sink (client specification 8.1 to 8.3): the
// format read into a layout, the whole-frame ring, the pre-roll, and the
// renderer's sound.

#include "audio/AudioLayout.hpp"
#include "audio/AudioRenderer.hpp"
#include "audio/FrameRing.hpp"
#include "audio/MixerControls.hpp"
#include "audio/PreRoll.hpp"

#include "audio.pb.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace beebium::linux_client;
using Catch::Approx;

namespace {

void add_source(beebium::AudioFormat& format, std::uint32_t index, const char* name,
                std::uint32_t group, bool bipolar, std::vector<const char*> channels) {
    auto* source = format.add_sources();
    source->set_source_index(index);
    source->set_source_name(name);
    source->set_encoding(beebium::ENCODING_2X16BIT_SIGNED);
    source->set_group_id(group);
    source->set_bipolar(bipolar);
    for (const char* channel : channels) {
        source->add_channel_names(channel);
    }
}

void add_group(beebium::AudioFormat& format, std::uint32_t id, const char* name) {
    auto* group = format.add_groups();
    group->set_group_id(id);
    group->set_group_name(name);
}

// The server's SN76489 alone: two unipolar fields of two channels.
beebium::AudioFormat sn_only() {
    beebium::AudioFormat format;
    format.set_sample_rate(48000);
    format.set_source_count(2);
    add_group(format, 1, "Internal Sound");
    add_source(format, 0, "SN76489", 1, false, {"Tone 0", "Tone 1"});
    add_source(format, 1, "SN76489", 1, false, {"Tone 2", "Noise"});
    return format;
}

// With a Music 5000 on field 2.
beebium::AudioFormat with_music_5000() {
    beebium::AudioFormat format = sn_only();
    format.set_source_count(4);
    add_group(format, 2, "Music 5000");
    add_source(format, 2, "Music5000", 2, true, {"Left", "Right"});
    auto* silent = format.add_sources();
    silent->set_source_index(3);
    silent->set_encoding(beebium::ENCODING_SILENCE);
    return format;
}

std::uint32_t pack(std::int16_t low, std::int16_t high) {
    return static_cast<std::uint16_t>(low) | (static_cast<std::uint32_t>(static_cast<std::uint16_t>(high)) << 16);
}

// Render `frames` of sound after the pre-roll has filled, returning the
// interleaved output of those frames.
struct Rig {
    AudioLayout layout;
    FrameRing ring;
    MixerControls controls;
    AudioRenderer renderer;

    explicit Rig(const beebium::AudioFormat& format)
        : layout(audio_layout(format)),
          ring(PreRoll::frames(PreRoll::CAPACITY_MS, 48000), layout.field_count),
          renderer(layout, ring, controls) {}

    // Feed frames made by `make(i)` and render them all.
    template <typename Make>
    std::vector<float> play(std::size_t frames, Make make) {
        std::vector<std::uint32_t> words(frames * layout.field_count, 0);
        for (std::size_t i = 0; i < frames; ++i) {
            make(i, &words[i * layout.field_count]);
        }
        // As the stream and the sink run: the first 120 ms and a little
        // more, then a chunk written for each chunk played.
        std::size_t written = std::min<std::size_t>(frames, 6144);
        ring.write(words.data(), written);
        std::vector<float> out(2 * frames, 0.0f);
        for (std::size_t played = 0; played < frames;) {
            const std::size_t chunk = std::min<std::size_t>(1024, frames - played);
            renderer.render(&out[2 * played], chunk);
            played += chunk;
            if (written < frames) {
                const std::size_t more = std::min<std::size_t>(1024, frames - written);
                ring.write(&words[written * layout.field_count], more);
                written += more;
            }
        }
        return out;
    }
};

float peak(const std::vector<float>& out, std::size_t from, int side) {
    float most = 0.0f;
    for (std::size_t i = from; i < out.size() / 2; ++i) {
        most = std::max(most, std::fabs(out[2 * i + static_cast<std::size_t>(side)]));
    }
    return most;
}

// One channel at full volume: a square wave between 0 and the chip's full
// scale, 16384, at `hz`.
auto square_on(std::size_t channel, double hz) {
    return [channel, hz](std::size_t i, std::uint32_t* frame) {
        const bool high = std::fmod(static_cast<double>(i) * hz / 48000.0, 1.0) < 0.5;
        std::int16_t halves[4] = {0, 0, 0, 0};
        halves[channel] = high ? 16384 : 0;
        frame[0] = pack(halves[0], halves[1]);
        frame[1] = pack(halves[2], halves[3]);
    };
}

}  // namespace

// --- the format -------------------------------------------------------------------

TEST_CASE("An SN76489-only format gives one group of four channels and no stereo field",
          "[audio][format]") {
    const AudioLayout layout = audio_layout(sn_only());
    CHECK(layout.field_count == 2);
    REQUIRE(layout.channels.size() == 4);
    CHECK(layout.channels[3].name == "Noise");
    CHECK(layout.channels[3].field == 1);
    CHECK(layout.channels[3].half == 1);
    CHECK(layout.stereo_fields.empty());
    REQUIRE(layout.groups.size() == 1);
    CHECK(layout.groups[0].has_channel_controls);
    CHECK(layout.groups[0].channels == std::vector<std::size_t>{0, 1, 2, 3});
}

TEST_CASE("A Music 5000 is a second group, a stereo field with a sub-master only",
          "[audio][format]") {
    const AudioLayout layout = audio_layout(with_music_5000());
    REQUIRE(layout.groups.size() == 2);
    CHECK(layout.groups[1].name == "Music 5000");
    CHECK_FALSE(layout.groups[1].has_channel_controls);
    REQUIRE(layout.stereo_fields.size() == 1);
    CHECK(layout.stereo_fields[0].field == 2);
    CHECK(layout.stereo_fields[0].group == 1);
    // The silent field is neither.
    CHECK(layout.channels.size() == 4);
}

TEST_CASE("The SN76489's fields are found from the format, wherever they are",
          "[audio][format]") {
    beebium::AudioFormat format;
    format.set_source_count(3);
    add_group(format, 7, "Music 3000");
    add_group(format, 9, "Internal Sound");
    add_source(format, 0, "Music3000", 7, true, {"Left", "Right"});
    add_source(format, 1, "SN76489", 9, false, {"Tone 0", "Tone 1"});
    add_source(format, 2, "SN76489", 9, false, {"Tone 2", "Noise"});
    const AudioLayout layout = audio_layout(format);
    REQUIRE(layout.channels.size() == 4);
    CHECK(layout.channels[0].field == 1);
    CHECK(layout.groups[1].has_channel_controls);
    REQUIRE(layout.stereo_fields.size() == 1);
    CHECK(layout.stereo_fields[0].field == 0);
}

TEST_CASE("Numbered channels are listed by number", "[audio][format]") {
    beebium::AudioFormat format;
    format.set_source_count(2);
    add_group(format, 1, "Voices");
    add_source(format, 0, "V", 1, false, {"3", "1"});
    add_source(format, 1, "V", 1, false, {"2", "0"});
    const AudioLayout layout = audio_layout(format);
    std::vector<std::string> names;
    for (std::size_t c : layout.groups[0].channels) {
        names.push_back(layout.channels[c].name);
    }
    CHECK(names == std::vector<std::string>{"0", "1", "2", "3"});
}

// --- the ring --------------------------------------------------------------------

TEST_CASE("The ring keeps whole frames, and an overflow drops whole frames", "[audio][ring]") {
    FrameRing ring(4, 3);
    const std::vector<std::uint32_t> frames{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    CHECK(ring.write(frames.data(), 5) == 4);
    CHECK(ring.dropped_frames() == 1);
    std::vector<std::uint32_t> out(6, 0);
    CHECK(ring.read(out.data(), 2) == 2);
    CHECK(out == std::vector<std::uint32_t>{1, 2, 3, 4, 5, 6});
    // Wrapping keeps every frame's fields together.
    CHECK(ring.write(frames.data() + 12, 1) == 1);
    std::vector<std::uint32_t> rest(9, 0);
    CHECK(ring.read(rest.data(), 3) == 3);
    CHECK(rest == std::vector<std::uint32_t>{7, 8, 9, 10, 11, 12, 13, 14, 15});
}

TEST_CASE("A short read is counted as an underrun", "[audio][ring]") {
    FrameRing ring(8, 2);
    const std::vector<std::uint32_t> frames(4, 0);
    ring.write(frames.data(), 2);
    std::vector<std::uint32_t> out(10, 0);
    CHECK(ring.read(out.data(), 5) == 2);
    CHECK(ring.underrun_events() == 1);
    CHECK(ring.underrun_frames() == 3);
}

// --- the pre-roll ------------------------------------------------------------------

TEST_CASE("Playback waits for the target, 120 ms, before it plays", "[audio][preroll]") {
    PreRoll pre_roll(48000);
    CHECK(pre_roll.target_milliseconds() == 120);
    CHECK(pre_roll.frames_to_pull(5759, 512) == 0);
    CHECK(pre_roll.frames_to_pull(5760, 512) == 512);
    // Once playing, it pulls whatever is there.
    CHECK(pre_roll.frames_to_pull(10, 512) == 512);
}

TEST_CASE("An underrun raises the target by 60 ms to at most 480 ms, and re-primes",
          "[audio][preroll]") {
    PreRoll pre_roll(48000);
    pre_roll.frames_to_pull(6000, 512);
    pre_roll.note_underrun();
    CHECK(pre_roll.target_milliseconds() == 180);
    CHECK(pre_roll.priming());
    for (int i = 0; i < 20; ++i) {
        pre_roll.note_underrun();
    }
    CHECK(pre_roll.target_milliseconds() == 480);
    // Held: no decay however long it plays.
    pre_roll.frames_to_pull(48000, 512);
    for (int i = 0; i < 10000; ++i) {
        pre_roll.frames_to_pull(48000, 512);
    }
    CHECK(pre_roll.target_milliseconds() == 480);
    pre_roll.reset();
    CHECK(pre_roll.target_milliseconds() == 120);
    CHECK(pre_roll.priming());
}

// --- the sound ---------------------------------------------------------------------

TEST_CASE("Nothing plays until the pre-roll has filled", "[audio][render]") {
    Rig rig(sn_only());
    std::vector<std::uint32_t> words(2 * 1000, pack(16384, 0));
    rig.ring.write(words.data(), 1000);
    std::vector<float> out(2 * 512, 1.0f);
    rig.renderer.render(out.data(), 512);
    CHECK(std::all_of(out.begin(), out.end(), [](float s) { return s == 0.0f; }));
    CHECK(rig.ring.available() == 1000);  // nothing consumed while priming
}

TEST_CASE("One SN76489 channel at full volume plays at the macOS client's level",
          "[audio][render]") {
    Rig rig(sn_only());
    const auto out = rig.play(48000, square_on(0, 1000.0));
    // Unit AC amplitude through the high-pass, times the mix gain, times
    // the centre pan, 0.707: about 0.15 a side, both sides alike.
    const float left = peak(out, 24000, 0);
    CHECK(left == Approx(AudioRenderer::MIX_GAIN * std::sqrt(0.5f)).margin(0.01));
    CHECK(peak(out, 24000, 1) == Approx(left));
}

TEST_CASE("The SN76489's DC is removed", "[audio][render]") {
    Rig rig(sn_only());
    // A constant level: all DC, which the high-pass takes away.
    const auto out = rig.play(48000, [](std::size_t, std::uint32_t* frame) {
        frame[0] = pack(16384, 16384);
        frame[1] = pack(16384, 16384);
    });
    CHECK(peak(out, 24000, 0) < 0.001f);
}

TEST_CASE("A bipolar stereo field is mixed as it is, left and right apart", "[audio][render]") {
    Rig rig(with_music_5000());
    const auto out = rig.play(9600, [](std::size_t, std::uint32_t* frame) {
        frame[2] = pack(16384, -8192);
    });
    // No high-pass: a held level stays.
    CHECK(out[2 * 9000] == Approx(0.5f * AudioRenderer::MUSIC_MIX_GAIN));
    CHECK(out[2 * 9000 + 1] == Approx(-0.25f * AudioRenderer::MUSIC_MIX_GAIN));
}

TEST_CASE("Muting a group silences it and nothing else", "[audio][render][mixer]") {
    Rig rig(with_music_5000());
    rig.controls.group_muted[0].store(true);  // Internal Sound
    const auto out = rig.play(48000, [](std::size_t i, std::uint32_t* frame) {
        square_on(0, 1000.0)(i, frame);
        frame[2] = pack(8192, 8192);
    });
    CHECK(out[2 * 40000] == Approx(0.25f * AudioRenderer::MUSIC_MIX_GAIN));
}

TEST_CASE("A channel at zero is silent, the others are not", "[audio][render][mixer]") {
    Rig rig(sn_only());
    rig.controls.channel_volume[3].store(0.0f);  // Noise
    const auto silent = rig.play(48000, square_on(3, 1000.0));
    CHECK(peak(silent, 24000, 0) == 0.0f);
    const auto heard = rig.play(48000, square_on(0, 1000.0));
    CHECK(peak(heard, 24000, 0) > 0.1f);
}

TEST_CASE("The master mute silences everything", "[audio][render][mixer]") {
    Rig rig(with_music_5000());
    rig.controls.muted.store(true);
    const auto out = rig.play(48000, [](std::size_t i, std::uint32_t* frame) {
        square_on(0, 1000.0)(i, frame);
        frame[2] = pack(8192, 8192);
    });
    CHECK(std::all_of(out.begin(), out.end(), [](float s) { return s == 0.0f; }));
}

TEST_CASE("The volume curve spans 40 dB", "[audio][mixer]") {
    CHECK(AudioRenderer::volume_curve(0.0f) == 0.0f);
    CHECK(AudioRenderer::volume_curve(1.0f) == 1.0f);
    CHECK(AudioRenderer::volume_curve(0.5f) == Approx(0.1f));
    CHECK(AudioRenderer::volume_curve(0.001f) == Approx(0.01f).epsilon(0.01));
}

TEST_CASE("The soft limiter is the identity to 0.8 and never reaches 1", "[audio][mixer]") {
    CHECK(AudioRenderer::soft_limit(0.5f) == 0.5f);
    CHECK(AudioRenderer::soft_limit(-0.8f) == -0.8f);
    CHECK(AudioRenderer::soft_limit(3.0f) < 1.0f);
    CHECK(AudioRenderer::soft_limit(3.0f) > 0.99f);
    CHECK(AudioRenderer::soft_limit(-3.0f) == -AudioRenderer::soft_limit(3.0f));
    CHECK(AudioRenderer::soft_limit(0.81f) > AudioRenderer::soft_limit(0.805f));
}

TEST_CASE("A stall costs one underrun, then playback re-primes at a higher target",
          "[audio][render][preroll]") {
    Rig rig(sn_only());
    std::vector<std::uint32_t> words(2 * 6000, 0);
    rig.ring.write(words.data(), 6000);
    std::vector<float> out(2 * 1024, 0.0f);
    // 6000 frames held, 120 ms (5760) needed: plays 5 callbacks, then runs dry.
    for (int i = 0; i < 6; ++i) {
        rig.renderer.render(out.data(), 1024);
    }
    CHECK(rig.ring.underrun_events() == 1);
    CHECK(rig.renderer.pre_roll().target_milliseconds() == 180);
    CHECK(rig.renderer.pre_roll().priming());
    // Priming: no further underruns while it refills.
    rig.renderer.render(out.data(), 1024);
    CHECK(rig.ring.underrun_events() == 1);
}

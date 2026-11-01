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

#ifndef BEEBIUM_LINUX_AUDIO_AUDIO_LAYOUT_HPP
#define BEEBIUM_LINUX_AUDIO_AUDIO_LAYOUT_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace beebium {
class AudioFormat;
}

namespace beebium::linux_client {

// What each 32-bit field of an audio frame carries, as the server's
// GetAudioFormat says (client specification 8.1): nothing about which
// extension adds sound is known here beforehand.
//
// Two kinds of field are rendered. A unipolar 2x16 signed field (the
// SN76489's) holds two channels whose zero is not the AC midpoint: each is
// DC-removed and has its own volume. A bipolar 2x16 signed field (a Music
// 5000 or 3000) is one stereo pair, low half left, mixed as it is. Any other
// field is left silent.
struct AudioLayout {
    static constexpr std::size_t MAX_CHANNELS = 16;
    static constexpr std::size_t MAX_STEREO_FIELDS = 8;
    static constexpr std::size_t MAX_GROUPS = 16;

    struct Channel {
        std::size_t field = 0;
        int half = 0;  // 0: the low 16 bits, 1: the high
        std::string name;
        std::size_t group = 0;  // index into groups
    };
    struct StereoField {
        std::size_t field = 0;
        std::size_t group = 0;
    };
    struct Group {
        std::uint32_t id = 0;
        std::string name;
        std::string description;
        std::string color;
        // The group owning per-channel sources: its channels get sliders
        // beneath its sub-master (8.2).
        bool has_channel_controls = false;
        // Its channels, in the order the sidebar lists them.
        std::vector<std::size_t> channels;
    };

    std::uint32_t sample_rate = 48000;
    std::size_t field_count = 0;
    std::vector<Channel> channels;
    std::vector<StereoField> stereo_fields;
    // In the format's order; every one is shown, never hidden (8.2).
    std::vector<Group> groups;

    // The group index of a group id, or groups.size() when there is none.
    std::size_t group_index(std::uint32_t id) const;
};

AudioLayout audio_layout(const beebium::AudioFormat& format);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_AUDIO_AUDIO_LAYOUT_HPP

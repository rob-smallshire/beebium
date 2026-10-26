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

#include <beebium/extension/AudioSourcePort.hpp>

#include <stdexcept>
#include <utility>

namespace beebium {

namespace {

// The SN76489's two fields: (tone0, tone1) and (tone2, noise), named with MOS
// SOUND channel numbers (0 is noise). Its channels are unipolar.
AudioFieldInfo sn76489_info(std::vector<std::string> channel_names) {
    AudioFieldInfo info;
    info.name = "SN76489";
    info.encoding = AudioFieldEncoding::Signed2x16;
    info.channel_names = std::move(channel_names);
    info.group_name = "Internal Sound";
    info.group_description = "BBC Micro built-in SN76489 sound chip";
    info.group_color = "#FF6B35";
    info.bipolar = false;
    return info;
}

}  // namespace

AudioSourcePort::AudioSourcePort() {
    taken_[0] = true;
    infos_[0] = sn76489_info({"1", "2"});
    taken_[1] = true;
    infos_[1] = sn76489_info({"3", "0"});
}

void AudioSourcePort::register_source(std::size_t field_index, AudioFieldSource& source,
                                      AudioFieldInfo info) {
    if (field_index >= kFieldCount) {
        throw std::runtime_error(
            "AudioSourcePort: audio frame field " + std::to_string(field_index)
            + " does not exist (fields 0-" + std::to_string(kFieldCount - 1) + ")");
    }
    if (taken_[field_index]) {
        throw std::runtime_error(
            "AudioSourcePort: audio frame field " + std::to_string(field_index)
            + " is already taken by " + infos_[field_index].name);
    }
    sources_[field_index] = &source;
    taken_[field_index] = true;
    infos_[field_index] = std::move(info);
}

bool AudioSourcePort::is_field_taken(std::size_t field_index) const {
    return field_index < kFieldCount && taken_[field_index];
}

const AudioFieldInfo* AudioSourcePort::field_info(std::size_t field_index) const {
    return is_field_taken(field_index) ? &infos_[field_index] : nullptr;
}

}  // namespace beebium

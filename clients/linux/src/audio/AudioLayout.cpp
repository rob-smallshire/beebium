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

#include "audio/AudioLayout.hpp"

#include "audio.pb.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace beebium::linux_client {

namespace {

std::optional<int> as_number(const std::string& text) {
    if (text.empty() || !std::all_of(text.begin(), text.end(),
                                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    return std::stoi(text);
}

}  // namespace

std::size_t AudioLayout::group_index(std::uint32_t id) const {
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].id == id) {
            return i;
        }
    }
    return groups.size();
}

AudioLayout audio_layout(const beebium::AudioFormat& format) {
    AudioLayout layout;
    layout.sample_rate = format.sample_rate() ? format.sample_rate() : 48000;
    layout.field_count = format.source_count();
    for (const auto& group : format.groups()) {
        if (layout.groups.size() == AudioLayout::MAX_GROUPS) {
            break;
        }
        layout.groups.push_back({group.group_id(), group.group_name(), group.description(),
                                 group.color(), false, {}});
    }
    // A source naming a group the format did not list still gets one, so
    // nothing audible is without a control.
    const auto group_for = [&layout](std::uint32_t id) {
        std::size_t index = layout.group_index(id);
        if (index == layout.groups.size() && layout.groups.size() < AudioLayout::MAX_GROUPS) {
            layout.groups.push_back({id, "Group " + std::to_string(id), {}, {}, false, {}});
        }
        return index;
    };

    std::vector<const beebium::AudioSource*> sources;
    for (const auto& source : format.sources()) {
        if (source.source_index() < layout.field_count) {
            sources.push_back(&source);
        }
    }
    std::sort(sources.begin(), sources.end(), [](const auto* a, const auto* b) {
        return a->source_index() < b->source_index();
    });
    for (const auto* source : sources) {
        if (source->encoding() != beebium::ENCODING_2X16BIT_SIGNED) {
            continue;  // not rendered: silent, or an encoding no source uses
        }
        const std::size_t group = group_for(source->group_id());
        if (group >= layout.groups.size()) {
            continue;
        }
        if (source->bipolar()) {
            if (layout.stereo_fields.size() < AudioLayout::MAX_STEREO_FIELDS) {
                layout.stereo_fields.push_back({source->source_index(), group});
            }
            continue;
        }
        for (int half = 0; half < 2; ++half) {
            if (layout.channels.size() == AudioLayout::MAX_CHANNELS) {
                break;
            }
            std::string name = half < source->channel_names_size()
                                   ? source->channel_names(half)
                                   : source->source_name() + " " + std::to_string(half);
            layout.channels.push_back({source->source_index(), half, std::move(name), group});
        }
    }

    for (std::size_t c = 0; c < layout.channels.size(); ++c) {
        auto& group = layout.groups[layout.channels[c].group];
        group.has_channel_controls = true;
        group.channels.push_back(c);
    }
    // As the macOS client lists them: in field order, unless every name is a
    // number, then by number.
    for (auto& group : layout.groups) {
        const bool numbered =
            std::all_of(group.channels.begin(), group.channels.end(), [&](std::size_t c) {
                return as_number(layout.channels[c].name).has_value();
            });
        if (numbered) {
            std::stable_sort(group.channels.begin(), group.channels.end(),
                             [&](std::size_t a, std::size_t b) {
                                 return *as_number(layout.channels[a].name) <
                                        *as_number(layout.channels[b].name);
                             });
        }
    }
    return layout;
}

}  // namespace beebium::linux_client

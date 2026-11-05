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

#include "preset/EconetStation.hpp"

#include <algorithm>
#include <charconv>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

bool parse_number(std::string_view text, long long& out) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc() && end == text.data() + text.size();
}

}  // namespace

EconetStation EconetStation::from_json(const ordered_json& value, const SchemaStation& schema) {
    EconetStation station;
    station.number = schema.min;
    station.low = schema.auto_low;
    station.high = schema.auto_high;
    if (value.is_number_integer()) {
        station.number = static_cast<std::uint32_t>(value.get<long long>());
        return station;
    }
    if (!value.is_string()) {
        return station;
    }
    const std::string text = value.get<std::string>();
    if (text == "auto") {
        station.mode = Mode::automatic;
        return station;
    }
    const std::string prefix = "auto:";
    const auto dash = text.find('-', prefix.size());
    long long low = 0;
    long long high = 0;
    if (text.rfind(prefix, 0) == 0 && dash != std::string::npos &&
        parse_number(std::string_view(text).substr(prefix.size(), dash - prefix.size()), low) &&
        parse_number(std::string_view(text).substr(dash + 1), high)) {
        station.mode = Mode::range;
        station.low = static_cast<std::uint32_t>(low);
        station.high = static_cast<std::uint32_t>(high);
    }
    return station;
}

ordered_json EconetStation::to_json() const {
    switch (mode) {
    case Mode::automatic:
        return "auto";
    case Mode::range:
        return "auto:" + std::to_string(low) + "-" + std::to_string(high);
    case Mode::number:
        break;
    }
    return number;
}

std::uint32_t EconetStation::clamp(const std::string& text, std::uint32_t was,
                                   const SchemaStation& schema) {
    long long value = 0;
    if (!parse_number(text, value)) {
        return was;
    }
    return static_cast<std::uint32_t>(
        std::clamp<long long>(value, schema.min, schema.max));
}

}  // namespace beebium::linux_client

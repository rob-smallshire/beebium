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

#include "connection/EconetPresentation.hpp"

#include <cctype>

namespace beebium::linux_client {

NetworkContent network_content(const EconetInputs& inputs) {
    if (!inputs.status) {
        return inputs.server_live ? NetworkContent::loading : NetworkContent::unavailable;
    }
    if (!inputs.status->enabled()) {
        return NetworkContent::not_fitted;
    }
    return NetworkContent::status;
}

bool status_current(const EconetInputs& inputs) {
    return inputs.status && inputs.server_live && !inputs.stream_lost;
}

std::string link_text(const EconetInputs& inputs) {
    if (!status_current(inputs)) {
        return "Unknown";
    }
    return inputs.status->connected() ? "Connected" : "Disconnected";
}

StationPresentation station_presentation(const beebium::GetEconetStatusResponse& status) {
    StationPresentation station;
    const std::uint32_t configured = status.station_id();
    const std::uint32_t in_force = status.station_in_force();
    station.in_force = in_force == 0 ? configured : in_force;
    if (in_force != 0 && configured != in_force) {
        station.pending = configured;
    }
    return station;
}

station_entry_result station_entry(const beebium::GetEconetStatusResponse& status,
                                   const std::string& text) {
    const std::uint32_t low = status.station_min();
    const std::uint32_t high = status.station_max();
    const std::string out_of_range = "Station number must be between " + std::to_string(low) +
                                     " and " + std::to_string(high) + ".";
    std::size_t start = text.find_first_not_of(' ');
    std::size_t end = text.find_last_not_of(' ');
    if (start == std::string::npos) {
        return {std::nullopt, out_of_range};
    }
    const std::string trimmed = text.substr(start, end - start + 1);
    if (trimmed.size() > 9) {
        return {std::nullopt, out_of_range};
    }
    std::uint64_t value = 0;
    for (const char c : trimmed) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return {std::nullopt, out_of_range};
        }
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value < low || value > high) {
        return {std::nullopt, out_of_range};
    }
    return {static_cast<std::uint32_t>(value), {}};
}

std::string station_placeholder(const beebium::GetEconetStatusResponse& status) {
    return std::to_string(status.station_min()) + "–" + std::to_string(status.station_max());
}

std::string collision_text(const EconetInputs& inputs) {
    if (!status_current(inputs) || inputs.status->aun_station_collision_count() == 0) {
        return {};
    }
    std::string text = "Station collision: " + inputs.status->aun_last_station_collision();
    if (inputs.status->aun_station_collision_count() > 1) {
        text += " (" + std::to_string(inputs.status->aun_station_collision_count()) + ")";
    }
    return text;
}

bool paused_banner(const EconetInputs& inputs) {
    return inputs.status && inputs.status->gated_by_speed();
}

}  // namespace beebium::linux_client

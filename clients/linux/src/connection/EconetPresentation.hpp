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

#ifndef BEEBIUM_LINUX_CONNECTION_ECONET_PRESENTATION_HPP
#define BEEBIUM_LINUX_CONNECTION_ECONET_PRESENTATION_HPP

#include "econet.pb.h"

#include <cstdint>
#include <optional>
#include <string>

namespace beebium::linux_client {

// What the Network mode shows (client specification 12.4), decided from the
// server's Econet status alone, as the macOS client's
// NetworkSidebarPresentation decides it. Nothing here knows a transport.

struct EconetInputs {
    std::optional<beebium::GetEconetStatusResponse> status;  // the last snapshot
    bool server_live = false;
    bool stream_lost = false;  // the status stream ended while the server was live
};

enum class NetworkContent { loading, unavailable, not_fitted, status };

NetworkContent network_content(const EconetInputs& inputs);

// Whether the last snapshot is the server's present state.
bool status_current(const EconetInputs& inputs);

// "Connected", "Disconnected", or "Unknown" when the status is not current.
std::string link_text(const EconetInputs& inputs);

// The station number in force, and the configured one while it waits for
// the next Break. An in-force number of 0 (not yet reported) is taken as
// settled on the configured one.
struct StationPresentation {
    std::uint32_t in_force = 0;
    std::optional<std::uint32_t> pending;
};
StationPresentation station_presentation(const beebium::GetEconetStatusResponse& status);

// A station number typed by the user, checked against the server's bounds.
struct station_entry_result {
    std::optional<std::uint32_t> value;
    std::string error;
    friend bool operator==(const station_entry_result&, const station_entry_result&) = default;
};
station_entry_result station_entry(const beebium::GetEconetStatusResponse& status,
                                   const std::string& text);
std::string station_placeholder(const beebium::GetEconetStatusResponse& status);

// The collisions in effect now, empty when none or the status is not
// current: a gauge, not a running total.
std::string collision_text(const EconetInputs& inputs);

// The transport needs real time and the speed is not 1x, so it is severed.
bool paused_banner(const EconetInputs& inputs);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_ECONET_PRESENTATION_HPP

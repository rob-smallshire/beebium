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

// What the Network mode shows (client specification 12.4), as the macOS
// client's NetworkSidebarPresentation decides it, from the server's Econet
// status alone.

#include "connection/EconetPresentation.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

beebium::GetEconetStatusResponse fitted(std::uint32_t configured, std::uint32_t in_force) {
    beebium::GetEconetStatusResponse status;
    status.set_has_econet_socket(true);
    status.set_enabled(true);
    status.set_station_id(configured);
    status.set_station_in_force(in_force);
    status.set_station_min(1);
    status.set_station_max(254);
    status.set_connected(true);
    return status;
}

}  // namespace

TEST_CASE("The mode's content follows the status, the server and the stream",
          "[econet][presentation]") {
    EconetInputs inputs;
    inputs.server_live = true;
    CHECK(network_content(inputs) == NetworkContent::loading);
    inputs.server_live = false;
    CHECK(network_content(inputs) == NetworkContent::unavailable);
    beebium::GetEconetStatusResponse unfitted;
    unfitted.set_has_econet_socket(true);
    inputs.status = unfitted;
    CHECK(network_content(inputs) == NetworkContent::not_fitted);
    inputs.status = fitted(254, 254);
    CHECK(network_content(inputs) == NetworkContent::status);
}

TEST_CASE("The link reads Connected or Disconnected, and Unknown once the status is not current",
          "[econet][presentation]") {
    EconetInputs inputs;
    inputs.server_live = true;
    inputs.status = fitted(254, 254);
    CHECK(link_text(inputs) == "Connected");
    inputs.status->set_connected(false);
    CHECK(link_text(inputs) == "Disconnected");
    inputs.stream_lost = true;
    CHECK(link_text(inputs) == "Unknown");
    inputs.stream_lost = false;
    inputs.server_live = false;
    CHECK(link_text(inputs) == "Unknown");
    CHECK_FALSE(status_current(inputs));
}

TEST_CASE("A station number set but not yet adopted is pending until the next Break",
          "[econet][presentation][station]") {
    const auto settled = station_presentation(fitted(254, 254));
    CHECK(settled.in_force == 254u);
    CHECK_FALSE(settled.pending.has_value());
    const auto pending = station_presentation(fitted(100, 254));
    CHECK(pending.in_force == 254u);
    REQUIRE(pending.pending.has_value());
    CHECK(*pending.pending == 100u);
    // An in-force number not yet reported is the configured one, settled.
    const auto unreported = station_presentation(fitted(100, 0));
    CHECK(unreported.in_force == 100u);
    CHECK_FALSE(unreported.pending.has_value());
}

TEST_CASE("A station number is checked against the server's bounds", "[econet][station]") {
    const auto status = fitted(254, 254);
    CHECK(station_entry(status, "17") == station_entry_result{17u, {}});
    const std::string out = "Station number must be between 1 and 254.";
    CHECK(station_entry(status, "0").error == out);
    CHECK(station_entry(status, "255").error == out);
    CHECK(station_entry(status, "abc").error == out);
    CHECK(station_entry(status, "").error == out);
    CHECK(station_entry(status, " 12 ").value == 12u);
    auto narrow = status;
    narrow.set_station_min(10);
    narrow.set_station_max(20);
    CHECK(station_entry(narrow, "9").error == "Station number must be between 10 and 20.");
    CHECK(station_placeholder(narrow) == "10–20");
}

TEST_CASE("The collision gauge shows what is in effect now, cleared with the stream",
          "[econet][presentation][collision]") {
    EconetInputs inputs;
    inputs.server_live = true;
    inputs.status = fitted(254, 254);
    CHECK(collision_text(inputs).empty());
    inputs.status->set_aun_station_collision_count(1);
    inputs.status->set_aun_last_station_collision("0.254 at 192.0.2.7");
    CHECK(collision_text(inputs) == "Station collision: 0.254 at 192.0.2.7");
    inputs.status->set_aun_station_collision_count(3);
    CHECK(collision_text(inputs) == "Station collision: 0.254 at 192.0.2.7 (3)");
    inputs.stream_lost = true;
    CHECK(collision_text(inputs).empty());
}

TEST_CASE("The paused banner shows while the transport is gated by speed",
          "[econet][presentation][banner]") {
    EconetInputs inputs;
    inputs.server_live = true;
    inputs.status = fitted(254, 254);
    CHECK_FALSE(paused_banner(inputs));
    inputs.status->set_requires_real_time(true);
    CHECK_FALSE(paused_banner(inputs));
    inputs.status->set_gated_by_speed(true);
    CHECK(paused_banner(inputs));
}

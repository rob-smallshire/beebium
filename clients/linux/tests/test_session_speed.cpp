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

// Emulation speed through the session (client specification 5.6): the
// multiplier sent, and the pacing statistics streamed only while someone
// watches them.

#include "app/MachineSession.hpp"
#include "app/SessionSpeed.hpp"

#include "FakeBeebiumServer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

void connect_session(MachineSession& session, FakeServer& server) {
    bool connected = false;
    auto connection = QObject::connect(&session, &MachineSession::connected_to_server,
                                       [&] { connected = true; });
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return connected; }));
    QObject::disconnect(connection);
}

beebium::PacingStats stats(double configured, double achieved, double estimated_max) {
    beebium::PacingStats sample;
    sample.set_speed_multiplier(configured);
    sample.set_achieved_speed_multiplier(achieved);
    sample.set_estimated_max_speed_multiplier(estimated_max);
    return sample;
}

}  // namespace

TEST_CASE("The pacing statistics stream only while they are watched", "[speed][pacing]") {
    FakeServer server;
    MachineSession session;
    connect_session(session, server);
    std::optional<PacingSample> heard;
    QObject::connect(&session.speed(), &SessionSpeed::sampled,
                     [&](const PacingSample& sample) { heard = sample; });

    // Nobody watching: no stream (event-driven, never polled).
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(server.system.pacing_watchers == 0);

    session.speed().set_watching(true);
    REQUIRE(wait_until([&] { return server.system.pacing_watchers == 1; }));
    CHECK(server.system.last_pacing_interval_ms == SessionSpeed::INTERVAL_MS);
    server.system.push_pacing(stats(2.0, 1.75, 40.0));
    REQUIRE(wait_until([&] { return heard.has_value(); }));
    CHECK(heard->configured == 2.0);
    CHECK(heard->achieved == 1.75);
    CHECK(heard->estimated_max == 40.0);

    session.speed().set_watching(false);
    REQUIRE(wait_until([&] { return server.system.pacing_watchers == 0; }));

    // Watching again opens a fresh stream.
    session.speed().set_watching(true);
    REQUIRE(wait_until([&] { return server.system.pacing_watchers == 1; }));
    session.disconnect_from_server();
    REQUIRE(wait_until([&] { return server.system.pacing_watchers == 0; }));
}

TEST_CASE("Watching before the connection starts the stream when it connects",
          "[speed][pacing]") {
    FakeServer server;
    MachineSession session;
    session.speed().set_watching(true);
    connect_session(session, server);
    REQUIRE(wait_until([&] { return server.system.pacing_watchers == 1; }));
    session.disconnect_from_server();
}

TEST_CASE("The multiplier reaches the server, 0.0 meaning unlimited", "[speed]") {
    FakeServer server;
    MachineSession session;
    connect_session(session, server);
    session.speed().set_multiplier(0.0);
    session.speed().set_multiplier(4.0);
    REQUIRE(wait_until([&] {
        std::lock_guard lock(server.system.calls_mutex);
        return server.system.speed_requests.size() == 2;
    }));
    std::lock_guard lock(server.system.calls_mutex);
    CHECK(server.system.speed_requests == std::vector<double>{0.0, 4.0});
}

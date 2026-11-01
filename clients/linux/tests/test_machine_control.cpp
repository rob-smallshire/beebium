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

// Machine control through the session (client specification 4.5, 5.4, 5.5,
// 5.7), against an in-process server.

#include "app/MachineSession.hpp"

#include "FakeBeebiumServer.hpp"

#include <catch2/catch_test_macros.hpp>

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

}  // namespace

TEST_CASE("A machine waiting for Run is reported as not running", "[control][run]") {
    FakeServer server;
    MachineSession session;
    connect_session(session, server);
    CHECK_FALSE(session.machine_running());
    CHECK(server.debugger.runs == 0);
}

TEST_CASE("A launched server is run on connect, once", "[control][run]") {
    FakeServer server;
    MachineSession session;
    session.set_launched_server(LaunchedServer{0, server.port, QStringLiteral("prov-1"), {}});
    bool running = false;
    QObject::connect(&session, &MachineSession::running_changed, [&](bool r) { running = r; });
    connect_session(session, server);
    REQUIRE(wait_until([&] { return running; }));
    CHECK(server.debugger.runs == 1);
    CHECK(session.machine_running());
}

TEST_CASE("Save sends the destination and the replace flag, and marks the machine saved",
          "[control][save]") {
    FakeServer server;
    MachineSession session;
    connect_session(session, server);
    CHECK_FALSE(session.machine_saved());
    std::optional<CallOutcome> outcome;
    QObject::connect(&session, &MachineSession::save_finished,
                     [&](const CallOutcome& o) { outcome = o; });
    session.save(QStringLiteral("/home/me/Elite.beebium-machine"), true);
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK(outcome->ok);
    CHECK(server.system.last_save_destination == "/home/me/Elite.beebium-machine");
    CHECK(server.system.last_save_replace);
    CHECK(session.machine_saved());
    CHECK(session.machine_dirpath() == "/home/me/Elite.beebium-machine");
}

TEST_CASE("A refused save keeps the server's words and leaves the machine as it was",
          "[control][save]") {
    FakeServer server;
    server.system.save_fails = true;
    MachineSession session;
    connect_session(session, server);
    std::optional<CallOutcome> outcome;
    QObject::connect(&session, &MachineSession::save_finished,
                     [&](const CallOutcome& o) { outcome = o; });
    session.save(QStringLiteral("/home/me/Taken.beebium-machine"), false);
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK_FALSE(outcome->ok);
    CHECK(outcome->code == grpc::StatusCode::ALREADY_EXISTS);
    CHECK(outcome->message == "destination exists");
    CHECK_FALSE(session.machine_saved());
}

TEST_CASE("Rename and battery reset reach the server", "[control]") {
    FakeServer server;
    MachineSession session;
    connect_session(session, server);
    std::optional<CallOutcome> named;
    QStringList units;
    QObject::connect(&session, &MachineSession::name_set,
                     [&](const CallOutcome& o) { named = o; });
    QObject::connect(&session, &MachineSession::battery_reset,
                     [&](const CallOutcome&, const QStringList& u) { units = u; });
    session.set_name(QStringLiteral("Elite Server"));
    session.reset_battery();
    REQUIRE(wait_until([&] { return named.has_value() && !units.isEmpty(); }));
    CHECK(server.system.last_name == "Elite Server");
    CHECK(units == QStringList{QStringLiteral("integra-b-rtc")});
}

TEST_CASE("Shutdown stops the streams first and carries this client's authority",
          "[control][shutdown]") {
    FakeServer server;
    MachineSession session;
    session.set_launched_server(LaunchedServer{0, server.port, QStringLiteral("prov-7"), {}});
    connect_session(session, server);
    std::optional<CallOutcome> answer;
    QObject::connect(&session, &MachineSession::shutdown_answered,
                     [&](const CallOutcome& o) { answer = o; });
    session.request_shutdown();
    REQUIRE(wait_until([&] { return answer.has_value(); }));
    CHECK(answer->ok);
    CHECK(server.system.shutdown_requests == 1);
    CHECK(server.system.shutdown_authority == "prov-7");
    // The streams went first: the status stream no longer drives liveness.
    server.system.push(std::nullopt);
    wait_until([] { return false; }, 200ms);
    CHECK(session.liveness() == Liveness::active);
    session.disconnect_from_server();
}

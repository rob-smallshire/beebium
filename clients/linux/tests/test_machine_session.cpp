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

// MachineSession against in-process fakes of the services it uses: the
// connection's events, liveness (client specification 4.3), identity (5.7),
// filesystem sharing (3.2) and the guest-ready sync point (7.3).

#include "app/MachineSession.hpp"

#include "FakeBeebiumServer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

// Records what a session reports.
struct Observer {
    int connected = 0;
    int guest_ready = 0;
    int resets = 0;
    std::vector<Liveness> liveness;
    QString name;
    QString description;
    std::optional<ConnectionFailure> failure;

    explicit Observer(MachineSession& session) {
        QObject::connect(&session, &MachineSession::connected_to_server, [this] { ++connected; });
        QObject::connect(&session, &MachineSession::guest_ready, [this] { ++guest_ready; });
        QObject::connect(&session, &MachineSession::machine_reset, [this] { ++resets; });
        QObject::connect(&session, &MachineSession::liveness_changed,
                         [this](Liveness l) { liveness.push_back(l); });
        QObject::connect(&session, &MachineSession::identity_changed,
                         [this](const QString& n, const QString& d) {
                             name = n;
                             description = d;
                         });
        QObject::connect(&session, &MachineSession::connection_failed,
                         [this](ConnectionFailure f, const QString&) { failure = f; });
    }
};

}  // namespace

TEST_CASE("Connecting reports the machine's identity and that it shares our filesystem",
          "[session]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    CHECK(observer.name == "Model B #1");
    CHECK(observer.description == "Station 1.254");
    CHECK(session.machine_name() == "Model B #1");
    CHECK(session.server_shares_filesystem());
    CHECK(session.liveness() == Liveness::active);
}

TEST_CASE("A server on another host does not share our filesystem", "[session]") {
    FakeServer server;
    server.system.host_fingerprint = "somewhere-else";
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    CHECK_FALSE(session.server_shares_filesystem());
}

TEST_CASE("Identity changes and resets arrive from the status stream", "[session]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));

    auto renamed = event(beebium::SERVER_STATUS_IDENTITY_CHANGED);
    renamed.mutable_identity()->set_name("Elite Server");
    renamed.mutable_identity()->set_description("");
    server.system.push(renamed);
    server.system.push(event(beebium::SERVER_STATUS_MACHINE_RESET));
    REQUIRE(wait_until([&] { return observer.resets == 1; }));
    CHECK(observer.name == "Elite Server");
    CHECK(session.machine_name() == "Elite Server");
}

TEST_CASE("Guest readiness syncs once per rising edge, including already ready",
          "[session][caps-lock]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));

    server.system.push(event(beebium::SERVER_STATUS_READY, true));    // ready on connect
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT, true));
    server.system.push(event(beebium::SERVER_STATUS_MACHINE_RESET, false));
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT, false));
    server.system.push(event(beebium::SERVER_STATUS_GUEST_READY, true));
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT, true));
    REQUIRE(wait_until([&] { return observer.guest_ready == 2 && observer.resets == 1; }));
    QCoreApplication::processEvents();
    CHECK(observer.guest_ready == 2);
}

TEST_CASE("An announced shutdown and the stream's end read as stopped", "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    server.system.push(event(beebium::SERVER_STATUS_SHUTTING_DOWN));
    server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return session.liveness() == Liveness::stopped; }));
    CHECK(observer.liveness.back() == Liveness::stopped);
}

TEST_CASE("The stream's end with no announcement reads as died", "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    server.system.push(std::nullopt);
    REQUIRE(wait_until([&] { return session.liveness() == Liveness::died; }));
}

TEST_CASE("Silent heartbeats read as unreachable, and the next event recovers",
          "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    server.system.push(event(beebium::SERVER_STATUS_READY));
    REQUIRE(wait_until([&] { return !observer.liveness.empty() || session.liveness() == Liveness::active; }));
    // Let the event land, then pretend three seconds pass without another.
    wait_until([] { return false; }, 100ms);
    session.tick_liveness(LivenessTracker::Clock::now() + 3s);
    CHECK(session.liveness() == Liveness::unreachable);
    server.system.push(event(beebium::SERVER_STATUS_HEARTBEAT));
    REQUIRE(wait_until([&] { return session.liveness() == Liveness::active; }));
}

TEST_CASE("Our own disconnect leaves liveness as it was", "[session][liveness]") {
    FakeServer server;
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.connected == 1; }));
    session.disconnect_from_server();
    wait_until([] { return false; }, 100ms);
    CHECK(session.liveness() == Liveness::active);
    CHECK(observer.liveness.empty());
    CHECK_FALSE(session.connected());
    CHECK_FALSE(session.server_shares_filesystem());
}

TEST_CASE("A server speaking another protocol is refused as incompatible", "[session]") {
    FakeServer server;
    server.system.protocol_fingerprint = "another-protocol";
    MachineSession session;
    Observer observer(session);
    session.connect_to(server.endpoint());
    REQUIRE(wait_until([&] { return observer.failure.has_value(); }));
    CHECK(*observer.failure == ConnectionFailure::incompatible);
    CHECK(observer.connected == 0);
}

TEST_CASE("Nothing listening is unreachable", "[session]") {
    int port = 0;
    {
        FakeServer departed;
        port = departed.port;
    }
    MachineSession session;
    Observer observer(session);
    session.connect_to(QStringLiteral("127.0.0.1:%1").arg(port));
    REQUIRE(wait_until([&] { return observer.failure.has_value(); }, 10000ms));
    CHECK(*observer.failure == ConnectionFailure::unreachable);
}

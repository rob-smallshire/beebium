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

// The sideways memory through the session (client specification 11): read
// when watched, kept live from the event stream while watched, and
// protection changed by group and kind, then read back.

#include "app/MachineSession.hpp"
#include "app/SessionSideways.hpp"

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

// Slot 15 RAM with its own write switch, slot 14 ROM with a hide switch.
void script_board(FakeServer& server) {
    std::lock_guard lock(server.sideways.status_mutex);
    auto& status = server.sideways.status;
    status.Clear();
    for (std::uint32_t slot : {14u, 15u}) {
        auto* socket = status.add_sockets();
        socket->set_socket_index(slot);
        socket->add_aliased_slots(slot);
        socket->set_type(slot == 15 ? beebium::SIDEWAYS_SLOT_TYPE_RAM
                                    : beebium::SIDEWAYS_SLOT_TYPE_ROM);
    }
    auto* write = status.add_protection_groups();
    write->set_id("slot-15");
    write->add_slots(15);
    write->set_supports_write_protect(true);
    auto* hide = status.add_protection_groups();
    hide->set_id("socket-14");
    hide->add_slots(14);
    hide->set_supports_hide(true);
}

}  // namespace

TEST_CASE("The sideways state is read and streamed only while watched", "[sideways][session]") {
    FakeServer server;
    script_board(server);
    MachineSession session;
    connect_session(session, server);
    SessionSideways& sideways = session.sideways();
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(server.sideways.status_requests == 0);
    CHECK(server.sideways.watchers == 0);
    CHECK_FALSE(sideways.loaded());

    sideways.set_watching(true);
    REQUIRE(wait_until([&] { return sideways.loaded(); }));
    CHECK(sideways.topology().sockets.size() == 2);
    REQUIRE(wait_until([&] { return server.sideways.watchers == 1; }));
    CHECK(server.sideways.last_monitor_header_changes);

    sideways.set_watching(false);
    REQUIRE(wait_until([&] { return server.sideways.watchers == 0; }));
    // Shown again: read afresh, as it may have changed meanwhile.
    const int before = server.sideways.status_requests;
    sideways.set_watching(true);
    REQUIRE(wait_until([&] { return server.sideways.status_requests > before; }));
    session.disconnect_from_server();
    REQUIRE(wait_until([&] { return server.sideways.watchers == 0; }));
}

TEST_CASE("A *SRLOAD's new header updates the socket live; a reconfiguration re-reads",
          "[sideways][session]") {
    FakeServer server;
    script_board(server);
    MachineSession session;
    connect_session(session, server);
    SessionSideways& sideways = session.sideways();
    sideways.set_watching(true);
    REQUIRE(wait_until([&] { return server.sideways.watchers == 1 && sideways.loaded(); }));

    beebium::SidewaysEvent loaded;
    loaded.mutable_slot_header_changed()->set_slot(15);
    loaded.mutable_slot_header_changed()->mutable_rom_header()->set_recognised(true);
    loaded.mutable_slot_header_changed()->mutable_rom_header()->set_title("Wordwise");
    server.sideways.push(loaded);
    REQUIRE(wait_until([&] { return sideways.topology().sockets[0].rom.title == "Wordwise"; }));

    {
        std::lock_guard lock(server.sideways.status_mutex);
        server.sideways.status.mutable_sockets(0)->set_type(beebium::SIDEWAYS_SLOT_TYPE_EMPTY);
    }
    const int before = server.sideways.status_requests;
    beebium::SidewaysEvent configured;
    configured.mutable_slot_configured()->set_slot(14);
    server.sideways.push(configured);
    REQUIRE(wait_until([&] { return server.sideways.status_requests > before; }));
    REQUIRE(wait_until([&] {
        return sideways.topology().sockets[1].type == SidewaysSlotType::empty;
    }));
}

TEST_CASE("Protection is engaged by group and kind, then shown as the server reads it back",
          "[sideways][session]") {
    FakeServer server;
    script_board(server);
    MachineSession session;
    connect_session(session, server);
    SessionSideways& sideways = session.sideways();
    sideways.set_watching(true);
    REQUIRE(wait_until([&] { return sideways.loaded(); }));
    CHECK_FALSE(sideways.topology().groups[0].write_protected);

    sideways.set_protection("slot-15", SidewaysProtection::write_protect, true);
    // Unchanged until the read-back: never the click.
    CHECK_FALSE(sideways.topology().groups[0].write_protected);
    REQUIRE(wait_until([&] { return sideways.topology().groups[0].write_protected; }));
    {
        std::lock_guard lock(server.sideways.status_mutex);
        CHECK(server.sideways.last_group_id == "slot-15");
        CHECK(server.sideways.last_kind == beebium::SIDEWAYS_PROTECTION_KIND_WRITE_PROTECT);
        CHECK(server.sideways.last_engaged);
    }
    sideways.set_protection("socket-14", SidewaysProtection::hide, true);
    REQUIRE(wait_until([&] { return sideways.topology().groups[1].hidden; }));

    // Refused: said in the server's words, the state as it was.
    {
        std::lock_guard lock(server.sideways.status_mutex);
        server.sideways.refusal = "the switch is stuck";
    }
    std::optional<QString> refused;
    QObject::connect(&sideways, &SessionSideways::protection_refused,
                     [&](const QString& message) { refused = message; });
    sideways.set_protection("slot-15", SidewaysProtection::write_protect, false);
    REQUIRE(wait_until([&] { return refused.has_value(); }));
    CHECK(*refused == QStringLiteral("the switch is stuck"));
    CHECK(sideways.topology().groups[0].write_protected);
}

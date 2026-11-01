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

// Discovered machines as the Connect dialog lists them (client
// specification 4.1).

#include "app/DiscoveredMachines.hpp"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>

using namespace beebium::linux_client;

namespace {

beebium::discovery::DiscoveredService service(const char* name, const char* hostname,
                                              const char* address, std::uint16_t port) {
    beebium::discovery::DiscoveredService s;
    s.instance_name = name;
    s.hostname = hostname;
    s.ipv4_addr_net_byte_order = address ? inet_addr(address) : 0;
    s.port = port;
    s.txt_records = {{"uuid", "1234"}, {"model", "ModelB"},
                     {"model_name", "BBC Model B"}, {"role", "host"}};
    return s;
}

const ThisHost HOST{QStringLiteral("pi400"), {QStringLiteral("192.168.1.20"),
                                              QStringLiteral("127.0.0.1")}};

}  // namespace

TEST_CASE("A discovered service becomes a machine with its TXT keys", "[discovery]") {
    const auto machine =
        machine_from_service(service("Elite", "studio.local.", "192.168.1.30", 48876), HOST);
    CHECK(machine.instance_name == "Elite");
    CHECK(machine.address == "192.168.1.30");
    CHECK(machine.port == 48876);
    CHECK(machine.uuid == "1234");
    CHECK(machine.model_name == "BBC Model B");
    CHECK_FALSE(machine.on_this_host);
    CHECK(machine.endpoint() == Endpoint{"192.168.1.30", 48876});
}

TEST_CASE("A machine on this host is recognised by name or by address", "[discovery]") {
    CHECK(machine_from_service(service("A", "PI400.local.", "10.0.0.9", 1), HOST).on_this_host);
    CHECK(machine_from_service(service("B", "other.local.", "192.168.1.20", 1), HOST).on_this_host);
    CHECK_FALSE(machine_from_service(service("C", "other.local.", "10.0.0.9", 1), HOST).on_this_host);
}

TEST_CASE("Without an IPv4 address the host name is dialled", "[discovery]") {
    const auto machine = machine_from_service(service("D", "studio.local.", nullptr, 48875), HOST);
    CHECK(machine.endpoint() == Endpoint{"studio.local", 48875});
}

TEST_CASE("This host's machines come first, then by name", "[discovery]") {
    std::vector<DiscoveredMachine> machines{
        machine_from_service(service("Zeta", "other.local.", "10.0.0.2", 1), HOST),
        machine_from_service(service("Beta", "pi400.local.", "192.168.1.20", 2), HOST),
        machine_from_service(service("Alpha", "other.local.", "10.0.0.3", 3), HOST),
    };
    sort_for_display(machines);
    CHECK(machines[0].instance_name == "Beta");
    CHECK(machines[1].instance_name == "Alpha");
    CHECK(machines[2].instance_name == "Zeta");
}

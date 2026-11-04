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

// Addresses, recents and connect links, client specification 4.1.

#include "app/DeepLink.hpp"
#include "connection/Endpoint.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("host:port parses", "[endpoint]") {
    const auto endpoint = parse_endpoint("host-a.local:48876");
    REQUIRE(endpoint);
    CHECK(endpoint->host == "host-a.local");
    CHECK(endpoint->port == 48876);
    CHECK(endpoint->to_string() == "host-a.local:48876");
}

TEST_CASE("A bare host takes the default port", "[endpoint]") {
    CHECK(parse_endpoint("localhost") == Endpoint{"localhost", 48875});
    CHECK(parse_endpoint("  192.0.2.20  ") == Endpoint{"192.0.2.20", 48875});
}

TEST_CASE("IPv6 addresses are bracketed with a port", "[endpoint]") {
    CHECK(parse_endpoint("[::1]:49000") == Endpoint{"::1", 49000});
    CHECK(parse_endpoint("[fe80::1]") == Endpoint{"fe80::1", 48875});
    CHECK(parse_endpoint("::1") == Endpoint{"::1", 48875});
    CHECK(Endpoint{"::1", 49000}.to_string() == "[::1]:49000");
}

TEST_CASE("Anything else is refused, disabling Connect", "[endpoint]") {
    for (const char* text : {"", "   ", "host:", "host:0", "host:65536", "host:port",
                             ":48875", "bad host:1", "[::1", "[]:1", "[::1]x"}) {
        INFO(text);
        CHECK_FALSE(parse_endpoint(text).has_value());
    }
    CHECK(parse_endpoint("host:65535").has_value());
    CHECK(parse_endpoint("host:1").has_value());
}

TEST_CASE("Endpoints compare by their key, ignoring the host's case", "[endpoint]") {
    CHECK(Endpoint{"LocalHost", 48875}.key() == Endpoint{"localhost", 48875}.key());
    CHECK(Endpoint{"localhost", 48875}.key() != Endpoint{"localhost", 48876}.key());
}

TEST_CASE("A successful connection goes first in the recents, once", "[endpoint][recents]") {
    const std::vector<Endpoint> recents{{"a", 1}, {"b", 2}, {"c", 3}};
    auto updated = updated_recents(recents, {"B", 2}, 10);
    REQUIRE(updated.size() == 3);
    CHECK(updated[0] == Endpoint{"B", 2});
    CHECK(updated[1] == Endpoint{"a", 1});
    CHECK(updated[2] == Endpoint{"c", 3});

    updated = updated_recents(recents, {"d", 4}, 3);
    CHECK(updated.size() == 3);
    CHECK(updated[0] == Endpoint{"d", 4});
    CHECK(updated[2] == Endpoint{"b", 2});
}

TEST_CASE("A connect link names a server", "[deep-link]") {
    const auto link = parse_connect_link(QUrl(QStringLiteral(
        "beebium://connect?host=localhost&port=49152&run=1&provenance=abc&sidebar=storage")));
    REQUIRE(link);
    CHECK(link->endpoint == Endpoint{"localhost", 49152});
    CHECK(link->run == QStringLiteral("1"));
    CHECK(link->provenance == QStringLiteral("abc"));
    CHECK(link->sidebar == QStringLiteral("storage"));
}

TEST_CASE("A connect link's port defaults, and its options are optional", "[deep-link]") {
    const auto link = parse_connect_link(QUrl(QStringLiteral("beebium://connect?host=pi.local")));
    REQUIRE(link);
    CHECK(link->endpoint == Endpoint{"pi.local", 48875});
    CHECK_FALSE(link->run.has_value());
    CHECK_FALSE(link->sidebar.has_value());
}

TEST_CASE("An IPv6 host in a connect link", "[deep-link]") {
    const auto link =
        parse_connect_link(QUrl(QStringLiteral("beebium://connect?host=%3A%3A1&port=50000")));
    REQUIRE(link);
    CHECK(link->endpoint == Endpoint{"::1", 50000});
}

TEST_CASE("Anything but a well-formed connect link is refused", "[deep-link]") {
    for (const char* url : {"https://connect?host=a", "beebium://open?host=a",
                            "beebium://connect", "beebium://connect?host=a&port=0",
                            "beebium://connect?port=48875"}) {
        INFO(url);
        CHECK_FALSE(parse_connect_link(QUrl(QString::fromLatin1(url))).has_value());
    }
}

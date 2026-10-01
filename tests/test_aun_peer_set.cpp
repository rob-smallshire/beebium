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

// Unit tests for AunPeerSet: the provenance/precedence peer authority the AUN
// transport owns. These exercise the value type alone (no backend attached);
// the apply-to-backend wiring is covered by the AUN UI, gRPC and mDNS e2e
// tests.

#include <catch2/catch_test_macros.hpp>

#include "AunPeerSet.hpp"

#include <beebium/econet/AunBackend.hpp>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

using namespace beebium;

namespace {
uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return htonl((static_cast<uint32_t>(a) << 24) | (b << 16) | (c << 8) | d);
}
}  // namespace

TEST_CASE("AunPeerSet: a single entry resolves to itself", "[aun][peer-set]") {
    AunPeerSet peers;
    CHECK(peers.peer_count() == 0);

    bool changed = peers.set_peer(0, 254, ip(127, 0, 0, 1), 32768,
                                  AunPeerProvenance::Launch);
    CHECK(changed);
    CHECK(peers.peer_count() == 1);

    auto winner = peers.resolve(0, 254);
    REQUIRE(winner.has_value());
    CHECK(winner->ip_addr == ip(127, 0, 0, 1));
    CHECK(winner->port == 32768);

    auto list = peers.list_peers();
    REQUIRE(list.size() == 1);
    CHECK(list[0].net == 0);
    CHECK(list[0].stn == 254);
    CHECK(list[0].provenance == AunPeerProvenance::Launch);
}

TEST_CASE("AunPeerSet: precedence Api > Launch > MapFile > Discovered",
          "[aun][peer-set][precedence]") {
    AunPeerSet peers;

    // Add every source for one station, lowest precedence first, and watch the
    // winner climb as each higher source arrives.
    CHECK(peers.set_peer(0, 1, ip(10, 0, 0, 1), 1, AunPeerProvenance::Discovered));
    CHECK(peers.resolve(0, 1)->port == 1);

    CHECK(peers.set_peer(0, 1, ip(10, 0, 0, 2), 2, AunPeerProvenance::MapFile));
    CHECK(peers.resolve(0, 1)->port == 2);  // MapFile beats Discovered

    CHECK(peers.set_peer(0, 1, ip(10, 0, 0, 3), 3, AunPeerProvenance::Launch));
    CHECK(peers.resolve(0, 1)->port == 3);  // Launch beats MapFile

    CHECK(peers.set_peer(0, 1, ip(10, 0, 0, 4), 4, AunPeerProvenance::Api));
    CHECK(peers.resolve(0, 1)->port == 4);  // Api beats all

    // Still one resolved peer -- the three shadowed entries are retained but
    // invisible to the resolution.
    CHECK(peers.peer_count() == 1);
    auto list = peers.list_peers();
    REQUIRE(list.size() == 1);
    CHECK(list[0].provenance == AunPeerProvenance::Api);
    CHECK(list[0].port == 4);
}

TEST_CASE("AunPeerSet: a lower-precedence add behind a winner does not change it",
          "[aun][peer-set][precedence]") {
    AunPeerSet peers;
    CHECK(peers.set_peer(0, 254, ip(10, 0, 0, 1), 40001, AunPeerProvenance::Api));

    // A discovered announcement for the same station is recorded but shadowed:
    // the winner is unchanged, so set_peer reports no change.
    bool changed = peers.set_peer(0, 254, ip(10, 0, 0, 2), 50002,
                                  AunPeerProvenance::Discovered);
    CHECK_FALSE(changed);
    CHECK(peers.resolve(0, 254)->port == 40001);
    CHECK(peers.is_operator_configured(0, 254));
}

TEST_CASE("AunPeerSet: removing the winner falls back to the next source",
          "[aun][peer-set][fallback]") {
    // This is the behaviour the two-value model could not express and which the
    // design doc (aun-peer-map-file.md section 2.3) and the mDNS discovery doc
    // now agree on: taking away an operator entry reveals a discovered one.
    AunPeerSet peers;
    peers.set_peer(0, 254, ip(10, 0, 0, 9), 50002, AunPeerProvenance::Discovered);
    peers.set_peer(0, 254, ip(10, 0, 0, 1), 40001, AunPeerProvenance::Api);
    CHECK(peers.resolve(0, 254)->port == 40001);

    // Remove the Api entry -> the Discovered one takes over (fall-back), rather
    // than the station disappearing.
    bool changed = peers.remove_peer(0, 254, AunPeerProvenance::Api);
    CHECK(changed);
    REQUIRE(peers.resolve(0, 254).has_value());
    CHECK(peers.resolve(0, 254)->port == 50002);
    CHECK_FALSE(peers.is_operator_configured(0, 254));
    CHECK(peers.peer_count() == 1);

    // Remove the last (Discovered) entry -> the station is gone.
    changed = peers.remove_peer(0, 254, AunPeerProvenance::Discovered);
    CHECK(changed);
    CHECK_FALSE(peers.resolve(0, 254).has_value());
    CHECK(peers.peer_count() == 0);
}

TEST_CASE("AunPeerSet: removing a shadowed entry does not move the winner",
          "[aun][peer-set][fallback]") {
    AunPeerSet peers;
    peers.set_peer(0, 254, ip(10, 0, 0, 1), 40001, AunPeerProvenance::Api);
    peers.set_peer(0, 254, ip(10, 0, 0, 9), 50002, AunPeerProvenance::Discovered);

    // Removing the shadowed Discovered layer leaves the Api winner in place.
    bool changed = peers.remove_peer(0, 254, AunPeerProvenance::Discovered);
    CHECK_FALSE(changed);
    CHECK(peers.resolve(0, 254)->port == 40001);
    CHECK(peers.peer_count() == 1);
}

TEST_CASE("AunPeerSet: removing an absent source is a no-op", "[aun][peer-set]") {
    AunPeerSet peers;
    CHECK_FALSE(peers.remove_peer(0, 254, AunPeerProvenance::Api));

    peers.set_peer(0, 254, ip(10, 0, 0, 1), 40001, AunPeerProvenance::Launch);
    CHECK_FALSE(peers.remove_peer(0, 254, AunPeerProvenance::Api));  // Launch stays
    CHECK(peers.resolve(0, 254)->port == 40001);
}

TEST_CASE("AunPeerSet: re-adding the same source updates its endpoint",
          "[aun][peer-set]") {
    AunPeerSet peers;
    peers.set_peer(0, 254, ip(10, 0, 0, 1), 40001, AunPeerProvenance::Discovered);
    bool changed = peers.set_peer(0, 254, ip(10, 0, 0, 1), 40099,
                                  AunPeerProvenance::Discovered);
    CHECK(changed);  // the winning endpoint moved
    CHECK(peers.resolve(0, 254)->port == 40099);
    CHECK(peers.peer_count() == 1);
}

TEST_CASE("AunPeerSet: is_operator_configured excludes Discovered",
          "[aun][peer-set]") {
    AunPeerSet peers;
    peers.set_peer(0, 10, ip(10, 0, 0, 1), 1, AunPeerProvenance::Discovered);
    CHECK_FALSE(peers.is_operator_configured(0, 10));

    peers.set_peer(0, 20, ip(10, 0, 0, 2), 2, AunPeerProvenance::MapFile);
    CHECK(peers.is_operator_configured(0, 20));

    peers.set_peer(0, 30, ip(10, 0, 0, 3), 3, AunPeerProvenance::Launch);
    CHECK(peers.is_operator_configured(0, 30));

    peers.set_peer(0, 40, ip(10, 0, 0, 4), 4, AunPeerProvenance::Api);
    CHECK(peers.is_operator_configured(0, 40));
}

TEST_CASE("AunPeerSet: endpoint_in reports a specific source's entry",
          "[aun][peer-set]") {
    AunPeerSet peers;
    peers.set_peer(0, 254, ip(10, 0, 0, 1), 40001, AunPeerProvenance::Api);
    peers.set_peer(0, 254, ip(10, 0, 0, 9), 50002, AunPeerProvenance::Discovered);

    auto api = peers.endpoint_in(0, 254, AunPeerProvenance::Api);
    REQUIRE(api.has_value());
    CHECK(api->port == 40001);

    auto disc = peers.endpoint_in(0, 254, AunPeerProvenance::Discovered);
    REQUIRE(disc.has_value());
    CHECK(disc->port == 50002);

    CHECK_FALSE(peers.endpoint_in(0, 254, AunPeerProvenance::Launch).has_value());
    CHECK_FALSE(peers.endpoint_in(0, 99, AunPeerProvenance::Api).has_value());
}

TEST_CASE("AunPeerSet: clear_provenance drops one layer across all stations",
          "[aun][peer-set]") {
    AunPeerSet peers;
    peers.set_peer(0, 1, ip(10, 0, 0, 1), 1, AunPeerProvenance::Launch);
    peers.set_peer(0, 2, ip(10, 0, 0, 2), 2, AunPeerProvenance::Launch);
    peers.set_peer(0, 2, ip(10, 0, 0, 9), 9, AunPeerProvenance::Discovered);
    peers.set_peer(0, 3, ip(10, 0, 0, 3), 3, AunPeerProvenance::Api);
    CHECK(peers.peer_count() == 3);

    // Clearing Launch removes 0.1 outright, and demotes 0.2 to its Discovered
    // entry; the Api-only 0.3 is untouched.
    peers.clear_provenance(AunPeerProvenance::Launch);
    CHECK_FALSE(peers.resolve(0, 1).has_value());
    REQUIRE(peers.resolve(0, 2).has_value());
    CHECK(peers.resolve(0, 2)->port == 9);
    CHECK(peers.resolve(0, 3)->port == 3);
    CHECK(peers.peer_count() == 2);
}

TEST_CASE("AunPeerSet: distinct stations coexist", "[aun][peer-set]") {
    AunPeerSet peers;
    peers.set_peer(0, 1, ip(10, 0, 0, 1), 1, AunPeerProvenance::Launch);
    peers.set_peer(0, 2, ip(10, 0, 0, 2), 2, AunPeerProvenance::Discovered);
    peers.set_peer(3, 1, ip(10, 0, 0, 3), 3, AunPeerProvenance::Api);
    CHECK(peers.peer_count() == 3);
    CHECK(peers.resolve(0, 1)->port == 1);
    CHECK(peers.resolve(0, 2)->port == 2);
    CHECK(peers.resolve(3, 1)->port == 3);
    // (0,1) and (3,1) must not alias despite sharing a station number.
    CHECK(peers.resolve(0, 1)->port != peers.resolve(3, 1)->port);
}

TEST_CASE("AunPeerSet: local_net round-trips", "[aun][peer-set]") {
    AunPeerSet peers;
    CHECK(peers.local_net() == 0);
    peers.set_local_net(3);
    CHECK(peers.local_net() == 3);
}

TEST_CASE("AunPeerSet: attach applies the resolved view to the backend",
          "[aun][peer-set][attach]") {
    AunPeerSet peers;
    peers.set_peer(0, 100, ip(127, 0, 0, 1), 41000, AunPeerProvenance::Api);

    AunBackend backend(0, 1, 0);
    REQUIRE(backend.is_connected());
    peers.attach(&backend);
    CHECK(backend.peer_count() == 1);
    CHECK(backend.is_reachable(0, 100));

    // A later change is pushed through without re-attaching.
    peers.set_peer(0, 200, ip(127, 0, 0, 1), 42000, AunPeerProvenance::Launch);
    CHECK(backend.is_reachable(0, 200));
    peers.remove_peer(0, 100, AunPeerProvenance::Api);
    CHECK_FALSE(backend.is_reachable(0, 100));
}

TEST_CASE("AunPeerSet: re-attaching to a new backend re-applies the whole set",
          "[aun][peer-set][attach]") {
    // The backend-recreation path (#55): the peer set outlives a backend, and
    // attaching to a fresh one re-applies every resolved entry.
    AunPeerSet peers;
    peers.set_peer(0, 100, ip(127, 0, 0, 1), 41000, AunPeerProvenance::Api);

    AunBackend first(0, 1, 0);
    peers.attach(&first);
    CHECK(first.is_reachable(0, 100));

    AunBackend second(0, 1, 0);
    peers.attach(&second);
    CHECK(second.is_reachable(0, 100));  // re-applied to the new backend
}

TEST_CASE("AunPeerSet: station collisions are counted and the last kept",
          "[aun][peer-set][collision]") {
    AunPeerSet peers;
    CHECK(peers.station_collisions().count == 0);

    peers.note_station_collision("first");
    peers.note_station_collision("second 0.254");
    auto report = peers.station_collisions();
    CHECK(report.count == 2);
    CHECK(report.last == "second 0.254");
}

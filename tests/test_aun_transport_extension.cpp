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

// Tests for the AUN transport extension's peer ownership (#55): the peer set
// lives in the extension, so AddPeer/RemovePeer/SetConnected work before the
// backend exists and the entries survive the backend being brought up and
// re-created. Drives the extension's C++ API directly (the same surface the
// AunDispatcher calls on behalf of AunService).

#include <catch2/catch_test_macros.hpp>

#include "AunEconetTransportExtension.hpp"
#include "AunPeerSet.hpp"
#include "beebium/econet/AunBackend.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

using namespace beebium;

namespace {
uint32_t loopback_ip() { return htonl(INADDR_LOOPBACK); }
}  // namespace

TEST_CASE("AUN transport: AddPeer works before the backend exists and survives Enable",
          "[aun][transport][extension]") {
    AunEconetTransportExtension ext;
    // No create_backend yet: the transport is configured but not bound, exactly
    // the state EconetService.Enable with an aun_port finds (issue #54/#55).
    REQUIRE(ext.backend() == nullptr);

    // AddPeer against an inactive transport must succeed and be recorded, not
    // fail with "AUN backend is not active" as it did before.
    ext.add_api_peer(0, 254, loopback_ip(), 40001);
    REQUIRE(ext.peer_set().peer_count() == 1);
    REQUIRE(ext.peer_set().resolve(0, 254).has_value());
    CHECK(ext.peer_set().resolve(0, 254)->port == 40001);

    // Bring the backend up (as Enable does). The recorded Api peer is applied to
    // the live routing view, so the station is immediately routable.
    ext.set_config({{"port", "0"}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);
    auto* backend = static_cast<AunBackend*>(backend_owner.get());
    REQUIRE(ext.backend() == backend);

    CHECK(ext.peer_set().peer_count() == 1);
    CHECK(backend->peer_count() == 1);
    CHECK(backend->is_reachable(0, 254));
}

TEST_CASE("AUN transport: RemovePeer of an Api entry falls back to a discovered one",
          "[aun][transport][extension]") {
    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);

    // A discovered peer and an operator (Api) peer for the same station: the
    // operator one wins while present.
    ext.peer_set().set_peer(0, 254, loopback_ip(), 50002,
                            AunPeerProvenance::Discovered);
    ext.add_api_peer(0, 254, loopback_ip(), 40001);
    CHECK(ext.peer_set().resolve(0, 254)->port == 40001);

    // Removing the Api entry reveals the discovered one rather than dropping the
    // station (the fall-back the two-value model could not express).
    ext.remove_api_peer(0, 254);
    REQUIRE(ext.peer_set().resolve(0, 254).has_value());
    CHECK(ext.peer_set().resolve(0, 254)->port == 50002);
    CHECK(ext.backend()->peer_endpoint(0, 254).has_value());
}

TEST_CASE("AUN transport: a peer edit after the backend is freed is safe; Api survives re-enable",
          "[aun][transport][extension]") {
    // DisableEconet frees the AunBackend via EconetSocket; nothing else tells
    // the transport, so its raw backend pointer (and the peer set's) would
    // dangle and every later discovery/API write would be a use-after-free.
    // The backend's destruction callback must detach the peer set and drop the
    // pointer first. (Run under -DBEEBIUM_ENABLE_SANITIZERS=ON to prove it.)
    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);

    ext.add_api_peer(0, 254, loopback_ip(), 40001);
    REQUIRE(ext.backend() != nullptr);
    REQUIRE(ext.backend()->is_reachable(0, 254));

    // Free the backend, exactly as EconetSocket::disable() does.
    backend_owner.reset();
    CHECK(ext.backend() == nullptr);  // destruction callback cleared it

    // Writes that previously dereferenced the freed backend are now no-ops on
    // the routing view, but still update the desired peer set: a discovery
    // write (browser thread) ...
    ext.peer_set().set_peer(0, 100, loopback_ip(), 50000,
                            AunPeerProvenance::Discovered);
    // ... and API edits (gRPC thread). None may touch freed storage.
    ext.add_api_peer(0, 253, loopback_ip(), 40002);
    ext.remove_api_peer(0, 253);
    CHECK(ext.peer_set().resolve(0, 254).has_value());  // Api 0.254 retained

    // Re-enable: a fresh backend re-applies the surviving Api peer (the
    // Discovered 0.100 was re-seeded away), routable again.
    auto second = ext.create_backend(/*station=*/1);
    REQUIRE(second != nullptr);
    CHECK(ext.backend()->is_reachable(0, 254));
    CHECK_FALSE(ext.backend()->is_reachable(0, 100));
}

TEST_CASE("AUN transport: SetConnected before the backend exists applies when it comes up",
          "[aun][transport][extension]") {
    AunEconetTransportExtension ext;
    // No backend yet: SetConnected(false) is remembered, not applied.
    CHECK_FALSE(ext.set_desired_connected(false));
    CHECK_FALSE(ext.desired_connected());

    ext.set_config({{"port", "0"}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);
    // The recorded desired state is applied: the freshly-bound socket comes up
    // disconnected because SetConnected(false) arrived first.
    CHECK_FALSE(ext.backend()->is_connected());

    // And a later SetConnected(true) now applies immediately (backend present).
    CHECK(ext.set_desired_connected(true));
    CHECK(ext.backend()->is_connected());
}

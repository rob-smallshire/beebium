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

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

using namespace beebium;

namespace {
uint32_t loopback_ip() { return htonl(INADDR_LOOPBACK); }

// A unique temp path for a hermetic map file.
std::filesystem::path temp_map_filepath() {
    std::random_device rd;
    auto name = "beebium-aun-map-" + std::to_string(rd()) + ".json";
    return std::filesystem::temp_directory_path() / name;
}

void write_file(const std::filesystem::path& filepath, const std::string& text) {
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    out << text;
}

// Find a resolved peer by (net, stn) in the peer set's listing.
std::optional<AunPeerEntry> find_peer(const AunPeerSet& peers, uint8_t net,
                                      uint8_t stn) {
    for (const auto& p : peers.list_peers()) {
        if (p.net == net && p.stn == stn) return p;
    }
    return std::nullopt;
}
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

TEST_CASE("AUN transport: a map file is loaded as MapFile peers and subnet rules",
          "[aun][transport][extension][map-file]") {
    auto map_filepath = temp_map_filepath();
    write_file(map_filepath, R"({
      "peers": [
        {"net": 0, "station": 254, "host": "192.168.1.10", "port": 32768},
        {"net": 0, "station": 40, "host": "192.168.1.40", "port": 32769},
        {"net": 0, "station": 41, "host": "nonexistent.invalid", "port": 32768}
      ],
      "subnets": [{"net": 128, "subnet": "192.168.5.0/24"}]
    })");

    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}, {"map-file", map_filepath.string()}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);
    auto* backend = static_cast<AunBackend*>(backend_owner.get());

    CHECK(ext.map_file_path() == map_filepath.string());
    CHECK(ext.map_file_error().empty());
    CHECK(ext.map_file_entry_count() == 4);  // 3 peers + 1 subnet

    auto fs = find_peer(ext.peer_set(), 0, 254);
    REQUIRE(fs.has_value());
    CHECK(fs->provenance == AunPeerProvenance::MapFile);
    CHECK(fs->port == 32768);
    auto a5000 = find_peer(ext.peer_set(), 0, 40);
    REQUIRE(a5000.has_value());
    CHECK(a5000->port == 32769);

    // The subnet rule is applied: a station in net 128 is reachable by the
    // convention even with no explicit peer.
    CHECK(backend->is_reachable(128, 50));

    // The unresolvable host is kept for display as unreachable, not routed.
    CHECK_FALSE(find_peer(ext.peer_set(), 0, 41).has_value());
    auto unreachable = ext.unreachable_map_peers();
    REQUIRE(unreachable.size() == 1);
    CHECK(unreachable[0].stn == 41);
    CHECK(unreachable[0].host == "nonexistent.invalid");

    std::filesystem::remove(map_filepath);
}

TEST_CASE("AUN transport: ReloadMap re-reads an edited map file without restart",
          "[aun][transport][extension][map-file]") {
    auto map_filepath = temp_map_filepath();
    write_file(map_filepath,
               R"({"peers":[{"net":0,"station":254,"host":"192.168.1.10","port":32768}]})");

    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}, {"map-file", map_filepath.string()}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);
    REQUIRE(find_peer(ext.peer_set(), 0, 254).has_value());
    CHECK(ext.map_file_entry_count() == 1);

    // Edit the file: drop 254, add 200. A reload replaces the MapFile layer.
    write_file(map_filepath,
               R"({"peers":[{"net":0,"station":200,"host":"192.168.1.99","port":32768}]})");
    auto result = ext.reload_map_file();
    CHECK(result.reloaded);
    CHECK(result.error.empty());

    CHECK_FALSE(find_peer(ext.peer_set(), 0, 254).has_value());
    auto added = find_peer(ext.peer_set(), 0, 200);
    REQUIRE(added.has_value());
    CHECK(added->provenance == AunPeerProvenance::MapFile);

    // A file that disappears clears the MapFile layer.
    std::filesystem::remove(map_filepath);
    result = ext.reload_map_file();
    CHECK(result.reloaded);
    CHECK_FALSE(find_peer(ext.peer_set(), 0, 200).has_value());
    CHECK(ext.map_file_entry_count() == 0);
}

TEST_CASE("AUN transport: map-file=none disables the map file",
          "[aun][transport][extension][map-file]") {
    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}, {"map-file", "none"}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);
    CHECK(ext.map_file_path().empty());
    auto result = ext.reload_map_file();
    CHECK_FALSE(result.reloaded);  // disabled
}

TEST_CASE("AUN transport: a malformed map file is reported and keeps prior entries",
          "[aun][transport][extension][map-file]") {
    auto map_filepath = temp_map_filepath();
    write_file(map_filepath,
               R"({"peers":[{"net":0,"station":254,"host":"192.168.1.10","port":32768}]})");

    AunEconetTransportExtension ext;
    ext.set_config({{"port", "0"}, {"map-file", map_filepath.string()}});
    auto backend_owner = ext.create_backend(/*station=*/1);
    REQUIRE(backend_owner != nullptr);
    REQUIRE(find_peer(ext.peer_set(), 0, 254).has_value());

    // Corrupt the file, then reload: the error is reported and the prior entry
    // is kept (a bad edit does not drop a working table).
    write_file(map_filepath, R"({"peers":[}})");
    auto result = ext.reload_map_file();
    CHECK(result.reloaded);
    CHECK_FALSE(result.error.empty());
    CHECK_FALSE(ext.map_file_error().empty());
    CHECK(find_peer(ext.peer_set(), 0, 254).has_value());  // prior entry kept

    std::filesystem::remove(map_filepath);
}

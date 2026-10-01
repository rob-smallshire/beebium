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

// Unit tests for the aun-map.json parser. Pure parsing -- no sockets, no DNS;
// hosts are kept as written and resolved elsewhere.

#include <catch2/catch_test_macros.hpp>

#include "AunMapFile.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <filesystem>
#include <string>

#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

using namespace beebium;

namespace {
std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "aun-map" / name;
}
std::uint32_t ip(const char* dotted) {
    in_addr addr{};
    inet_pton(AF_INET, dotted, &addr);
    return addr.s_addr;
}
}  // namespace

TEST_CASE("AunMapFile: a valid file parses peers and subnets", "[aun][map-file]") {
    auto result = load_aun_map(fixture("valid.json"));
    REQUIRE(result.file_present);
    REQUIRE(result.error.empty());
    REQUIRE(result.map.has_value());

    const auto& map = *result.map;
    REQUIRE(map.peers.size() == 3);
    CHECK(map.peers[0].net == 0);
    CHECK(map.peers[0].stn == 254);
    CHECK(map.peers[0].host == "192.168.1.10");
    CHECK(map.peers[0].port == 32768);
    CHECK(map.peers[0].label == "PiEconetBridge file server");
    CHECK(map.peers[1].port == 32769);
    // A DNS name is kept verbatim (resolution happens later, off the parser).
    CHECK(map.peers[2].host == "risc-pc.local");
    CHECK(map.peers[2].label.empty());  // label is optional

    REQUIRE(map.subnets.size() == 1);
    CHECK(map.subnets[0].net == 128);
    CHECK(map.subnets[0].base_ip == ip("192.168.5.0"));
    CHECK(map.subnets[0].subnet_text == "192.168.5.0/24");
}

TEST_CASE("AunMapFile: unknown keys are ignored", "[aun][map-file]") {
    auto result = load_aun_map(fixture("unknown_keys.json"));
    REQUIRE(result.error.empty());
    REQUIRE(result.map.has_value());
    REQUIRE(result.map->peers.size() == 1);
    CHECK(result.map->peers[0].stn == 254);
}

TEST_CASE("AunMapFile: a duplicate (net, station) names both entries",
          "[aun][map-file]") {
    auto result = load_aun_map(fixture("duplicate_peer.json"));
    CHECK_FALSE(result.map.has_value());
    CHECK(result.error.find("peers[1]") != std::string::npos);
    CHECK(result.error.find("peers[0]") != std::string::npos);
    CHECK(result.error.find("254") != std::string::npos);
}

TEST_CASE("AunMapFile: malformed JSON reports a byte position", "[aun][map-file]") {
    auto result = load_aun_map(fixture("malformed.json"));
    CHECK_FALSE(result.map.has_value());
    CHECK(result.error.find("byte") != std::string::npos);
}

TEST_CASE("AunMapFile: an out-of-range station is rejected with its position",
          "[aun][map-file]") {
    auto result = load_aun_map(fixture("bad_range.json"));
    CHECK_FALSE(result.map.has_value());
    CHECK(result.error.find("peers[0]") != std::string::npos);
    CHECK(result.error.find("station") != std::string::npos);
}

TEST_CASE("AunMapFile: a non-/24 subnet is rejected", "[aun][map-file]") {
    auto result = load_aun_map(fixture("bad_subnet.json"));
    CHECK_FALSE(result.map.has_value());
    CHECK(result.error.find("/24") != std::string::npos);
}

TEST_CASE("AunMapFile: an absent file is not an error", "[aun][map-file]") {
    auto result = load_aun_map(fixture("does-not-exist.json"));
    CHECK_FALSE(result.file_present);
    CHECK(result.error.empty());
    REQUIRE(result.map.has_value());
    CHECK(result.map->peers.empty());
    CHECK(result.map->subnets.empty());
}

TEST_CASE("AunMapFile: a subnet host octet is masked to the /24 network",
          "[aun][map-file]") {
    // A subnet written with a non-zero host octet is masked to the network
    // address, so the convention is anchored on a.b.c.0 regardless.
    auto result = parse_aun_map(
        R"({"subnets": [{"net": 1, "subnet": "10.20.30.44/24"}]})", "test");
    REQUIRE(result.map.has_value());
    REQUIRE(result.map->subnets.size() == 1);
    CHECK(result.map->subnets[0].base_ip == ip("10.20.30.0"));
}

TEST_CASE("AunMapFile: a peer net uses the full 0..255 byte", "[aun][map-file]") {
    auto result = parse_aun_map(
        R"({"peers":[{"net":200,"station":40,"host":"10.0.0.40","port":32768},
                     {"net":255,"station":1,"host":"10.0.0.1","port":32768}]})",
        "test");
    REQUIRE(result.map.has_value());
    REQUIRE(result.map->peers.size() == 2);
    CHECK(result.map->peers[0].net == 200);
    CHECK(result.map->peers[1].net == 255);
}

TEST_CASE("AunMapFile: top level must be an object", "[aun][map-file]") {
    auto result = parse_aun_map("[1, 2, 3]", "test");
    CHECK_FALSE(result.map.has_value());
    CHECK(result.error.find("object") != std::string::npos);
}

TEST_CASE("AunMapFile: empty object is a valid empty map", "[aun][map-file]") {
    auto result = parse_aun_map("{}", "test");
    REQUIRE(result.map.has_value());
    CHECK(result.map->peers.empty());
    CHECK(result.map->subnets.empty());
}

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

#include "AunEconetTransportExtension.hpp"

#include "AunDiscoveryAnnouncer.hpp"
#include "AunDiscoverySubscriber.hpp"
#include "beebium/econet/AunPacket.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "AunDispatcher.hpp"
#endif

#ifndef BEEBIUM_VERSION
#define BEEBIUM_VERSION "unknown"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <charconv>
#include <iostream>
#include <sstream>
#include <string_view>

namespace beebium {

namespace {

// Split s on a single delimiter character. Empty trailing fields are
// preserved so callers can reliably tell a missing field from a malformed
// one.
std::vector<std::string> split_on(std::string_view s, char delim) {
    std::vector<std::string> result;
    std::string current;
    for (char c : s) {
        if (c == delim) {
            result.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    result.push_back(std::move(current));
    return result;
}

bool parse_uint(std::string_view s, unsigned long& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

}  // namespace

std::optional<std::uint16_t> AunEconetTransportExtension::parse_port(
        const std::string& value) {
    if (value.empty()) {
        return AUN_DEFAULT_PORT;
    }
    if (value == "none") {
        return std::nullopt;
    }
    unsigned long parsed = 0;
    if (!parse_uint(value, parsed) || parsed > 0xFFFF) {
        std::cerr << "AUN extension: invalid port '" << value
                  << "' -- using default " << AUN_DEFAULT_PORT << "\n";
        return AUN_DEFAULT_PORT;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::uint8_t AunEconetTransportExtension::parse_net(const std::string& value) {
    if (value.empty()) {
        return 0;
    }
    unsigned long parsed = 0;
    if (!parse_uint(value, parsed) || parsed > 127) {
        std::cerr << "AUN extension: invalid net '" << value
                  << "' (expected 0..127) -- using default 0\n";
        return 0;
    }
    return static_cast<std::uint8_t>(parsed);
}

std::vector<AunEconetTransportExtension::PeerSpec>
AunEconetTransportExtension::parse_map(std::span<const std::string> entries) {
    std::vector<PeerSpec> peers;

    // Each entry encodes one peer as "[net.]stn@ip@port". The framework
    // has already split the repeated map= tokens into separate entries;
    // here we just split each entry on '@'.
    for (const auto& entry : entries) {
        if (entry.empty()) continue;
        auto fields = split_on(entry, '@');
        if (fields.size() != 3) {
            std::cerr << "AUN extension: malformed map entry '" << entry
                      << "' (expected [net.]stn@ip@port, "
                         "e.g. 0.254@127.0.0.1@32768) -- skipping\n";
            continue;
        }

        // net may be either a bare station (e.g. "254", net implicit 0)
        // or "net.stn" (e.g. "0.254"). Match the legacy --aun-map format.
        std::uint8_t net = 0;
        std::uint8_t stn = 0;
        auto net_dot_stn = split_on(fields[0], '.');
        if (net_dot_stn.size() == 2) {
            unsigned long n = 0, s = 0;
            if (!parse_uint(net_dot_stn[0], n) || !parse_uint(net_dot_stn[1], s)
                || n > 0xFF || s > 0xFF) {
                std::cerr << "AUN extension: invalid net.stn '" << fields[0]
                          << "' in map entry -- skipping\n";
                continue;
            }
            net = static_cast<std::uint8_t>(n);
            stn = static_cast<std::uint8_t>(s);
        } else if (net_dot_stn.size() == 1) {
            unsigned long s = 0;
            if (!parse_uint(net_dot_stn[0], s) || s > 0xFF) {
                std::cerr << "AUN extension: invalid station '" << fields[0]
                          << "' in map entry -- skipping\n";
                continue;
            }
            stn = static_cast<std::uint8_t>(s);
        } else {
            std::cerr << "AUN extension: malformed net.stn '" << fields[0]
                      << "' in map entry -- skipping\n";
            continue;
        }

        // IP address: dotted-quad, parse via inet_pton.
        in_addr addr{};
        if (inet_pton(AF_INET, fields[1].c_str(), &addr) != 1) {
            std::cerr << "AUN extension: invalid IP address '" << fields[1]
                      << "' in map entry -- skipping\n";
            continue;
        }

        // Port: decimal uint16.
        unsigned long port = 0;
        if (!parse_uint(fields[2], port) || port == 0 || port > 0xFFFF) {
            std::cerr << "AUN extension: invalid port '" << fields[2]
                      << "' in map entry -- skipping\n";
            continue;
        }

        peers.push_back(PeerSpec{
            net, stn, addr.s_addr, static_cast<std::uint16_t>(port)});
    }

    return peers;
}

std::unique_ptr<NetworkBackend>
AunEconetTransportExtension::create_backend(std::uint8_t station) {
    auto port_value = config_value("port");
    auto port = parse_port(port_value ? std::string(*port_value) : std::string{});
    if (!port.has_value()) {
        // port=none -- explicitly disabled.
        return nullptr;
    }

    auto net_value = config_value("net");
    auto local_net = parse_net(net_value ? std::string(*net_value) : std::string{});

    auto backend = std::make_unique<AunBackend>(local_net, station, *port);
    if (!backend->is_connected()) {
        // Keep the specific reason (port + OS cause) so AunUi can show it in
        // the sidebar instead of a bare "unavailable"; full detail is already
        // on stderr from AunBackend.
        unavailable_reason_ = backend->bind_error();
        std::cerr << "AUN extension: failed to bind UDP socket on port "
                  << *port << " -- network disabled\n";
        return nullptr;
    }
    unavailable_reason_.clear();  // a working backend clears any prior reason

    auto map_entries = config_list("map");
    auto peers = parse_map(map_entries ? *map_entries : std::span<const std::string>{});
    for (const auto& p : peers) {
        backend->add_peer(p.net, p.stn, p.ip_addr_net_byte_order, p.port);
    }

    backend_ = backend.get();  // non-owning; ownership goes to EconetSocket

    // Publish a DNS-SD announcement so other AUN-capable peers can
    // discover us without an explicit --aun map= entry. Failure to
    // start (e.g. mDNS unavailable on this platform) is non-fatal --
    // the transport still works for explicitly-mapped peers.
    //
    // machine_uuid is injected by ServerMain so the announcement's
    // impl-identity TXT record matches the same UUID this server
    // publishes on its _beebium._tcp announcement -- a discovering
    // tool can correlate the two and know which AUN peer is which
    // Beebium machine.
    auto machine_uuid_value = config_value("machine_uuid");
    std::string machine_uuid = machine_uuid_value
        ? std::string(*machine_uuid_value)
        : std::string{};

    announcer_ = std::make_unique<AunDiscoveryAnnouncer>(
        local_net, station, backend->local_port(),
        std::string{"beebium"}, std::string{BEEBIUM_VERSION},
        std::move(machine_uuid));
    if (!announcer_->start()) {
        std::cerr << "AUN extension: mDNS announcement unavailable -- "
                     "discovery disabled\n";
    }

    // Subscribe to peer announcements published by other AUN-capable
    // stations and add them to our peer table as Discovered entries.
    // Operator-configured peers (added via --aun map= or
    // AunService::AddPeer) take precedence over discovered ones --
    // see AunBackend::add_peer's PeerSource handling.
    subscriber_ = std::make_unique<AunDiscoverySubscriber>(*backend, station);
    // Discovery callbacks fire on the browser's background thread.
    // mark_dirty is atomic; the View is then re-built (and re-pushed
    // to gRPC subscribers) on the ExtensionUiService poll thread,
    // which calls list_peers() under the peer-table lock. So a
    // discovered peer reaches the UI within the next poll interval
    // (~50ms) without the operator having to interact with anything.
    subscriber_->set_on_peers_changed([this] { ui_.mark_dirty(); });
    if (!subscriber_->start()) {
        std::cerr << "AUN extension: mDNS subscription unavailable -- "
                     "peer discovery disabled\n";
    }

    return backend;
}

AunEconetTransportExtension::AunEconetTransportExtension() = default;
AunEconetTransportExtension::~AunEconetTransportExtension() = default;

std::vector<ExtensionRpcDispatcher*> AunEconetTransportExtension::rpc_dispatchers() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!dispatcher_) {
        dispatcher_ = std::make_unique<AunDispatcher>(*this);
    }
    return {dispatcher_.get()};
#else
    return {};
#endif
}

}  // namespace beebium

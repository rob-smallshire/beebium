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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_DISPATCHER_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_DISPATCHER_HPP

// Hand-written ExtensionRpcDispatcher for AUN-specific operations (peer table
// management, cable-plug simulation, port reporting), served through the core's
// ExtensionRpc channel rather than a plugin-hosted gRPC service. The aun
// library therefore links protobuf (for these messages) but not gRPC. See
// docs/discussion/extension-rpc-channel.md.
//
// Validation and "backend not active" conditions are reported in-band (the
// response's success=false + error string, with an OK RpcStatus), exactly as
// the old AunServiceImpl did -- clients check response.success, not the
// transport status. A request that is not valid protobuf is the one case that
// maps to a non-OK RpcStatus (kRpcInvalidArgument).

#include "AunEconetTransportExtension.hpp"
#include "beebium/econet/AunBackend.hpp"

#include <beebium/extension/ExtensionRpc.hpp>

#include "aun.pb.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

#include <mutex>
#include <string>
#include <string_view>

namespace beebium {

class AunDispatcher final : public ExtensionRpcDispatcher {
public:
    explicit AunDispatcher(AunEconetTransportExtension& extension)
        : extension_(extension) {}

    std::string_view service_name() const override { return "AunService"; }

    RpcStatus invoke(std::string_view method, std::string_view request,
                     std::string& response, RpcContext& /*ctx*/) override {
        if (method == "SetConnected") {
            return handle<AunSetConnectedRequest, AunSetConnectedResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    // Works whether or not the socket is up: the desired state
                    // is recorded and applied when the backend comes up. Always
                    // succeeds; the error field notes a deferred request so a
                    // caller can tell it took effect now versus later.
                    bool applied = extension_.set_desired_connected(
                        req.connected());
                    resp.set_success(true);
                    if (!applied) {
                        resp.set_error(
                            "AUN transport not yet active; connection state "
                            "will apply when it comes up");
                    }
                });
        }
        if (method == "AddPeer") {
            return handle<AunAddPeerRequest, AunAddPeerResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    add_peer(req, resp);
                });
        }
        if (method == "RemovePeer") {
            return handle<AunRemovePeerRequest, AunRemovePeerResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    if (auto e = check_net(req.net()); !e.empty())
                        return fail(resp, e);
                    if (auto e = check_stn(req.stn()); !e.empty())
                        return fail(resp, e);
                    // Edits the desired peer set whether or not a backend is up.
                    bool removed = extension_.remove_api_peer(
                        static_cast<std::uint8_t>(req.net()),
                        static_cast<std::uint8_t>(req.stn()));
                    resp.set_success(true);
                    resp.set_removed(removed);
                });
        }
        if (method == "ListPeers") {
            return handle<AunListPeersRequest, AunListPeersResponse>(
                method, request, response, [&](const auto&, auto& resp) {
                    list_peers(resp);
                });
        }
        if (method == "GetStatus") {
            return handle<AunGetStatusRequest, AunGetStatusResponse>(
                method, request, response, [&](const auto&, auto& resp) {
                    // peer_count comes from the peer set, so it is reported even
                    // before a backend exists (peers added ahead of Enable).
                    resp.set_peer_count(static_cast<std::uint32_t>(
                        extension_.peer_set().peer_count()));
                    resp.set_map_file_path(extension_.map_file_path());
                    resp.set_map_file_entry_count(
                        extension_.map_file_entry_count());
                    resp.set_map_file_error(extension_.map_file_error());
                    resp.set_discovery_mode(
                        AunEconetTransportExtension::discovery_mode_name(
                            extension_.discovery_mode()));
                    auto* backend = extension_.backend();
                    if (!backend) {
                        // No socket yet -> no link, whatever cable state a
                        // SetConnected has recorded for when it comes up.
                        resp.set_connected(false);
                        return;  // local_port stays 0 until the socket is up
                    }
                    resp.set_connected(backend->is_connected());
                    resp.set_local_port(backend->local_port());
                });
        }
        if (method == "ReloadMap") {
            return handle<AunReloadMapRequest, AunReloadMapResponse>(
                method, request, response, [&](const auto&, auto& resp) {
                    auto result = extension_.reload_map_file();
                    resp.set_reloaded(result.reloaded);
                    resp.set_error(result.error);
                });
        }
        if (method == "AddMapPeer") {
            return handle<AunAddMapPeerRequest, AunAddMapPeerResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    if (auto e = check_net(req.net()); !e.empty())
                        return fail(resp, e);
                    if (auto e = check_stn(req.stn()); !e.empty())
                        return fail(resp, e);
                    if (auto e = check_port(req.port()); !e.empty())
                        return fail(resp, e);
                    auto result = extension_.add_map_peer(
                        static_cast<std::uint8_t>(req.net()),
                        static_cast<std::uint8_t>(req.stn()), req.host(),
                        static_cast<std::uint16_t>(req.port()), req.label());
                    if (!result.error.empty()) return fail(resp, result.error);
                    resp.set_success(true);
                });
        }
        if (method == "RemoveMapPeer") {
            return handle<AunRemoveMapPeerRequest, AunRemoveMapPeerResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    if (auto e = check_net(req.net()); !e.empty())
                        return fail(resp, e);
                    if (auto e = check_stn(req.stn()); !e.empty())
                        return fail(resp, e);
                    auto result = extension_.remove_map_peer(
                        static_cast<std::uint8_t>(req.net()),
                        static_cast<std::uint8_t>(req.stn()));
                    if (!result.error.empty()) return fail(resp, result.error);
                    resp.set_success(true);
                    resp.set_removed(result.removed);
                });
        }
        if (method == "AddMapSubnet") {
            return handle<AunAddMapSubnetRequest, AunAddMapSubnetResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    if (auto e = check_net(req.net()); !e.empty())
                        return fail(resp, e);
                    auto result = extension_.add_map_subnet(
                        static_cast<std::uint8_t>(req.net()), req.subnet(),
                        req.label());
                    if (!result.error.empty()) return fail(resp, result.error);
                    resp.set_success(true);
                });
        }
        if (method == "RemoveMapSubnet") {
            return handle<AunRemoveMapSubnetRequest, AunRemoveMapSubnetResponse>(
                method, request, response, [&](const auto& req, auto& resp) {
                    if (auto e = check_net(req.net()); !e.empty())
                        return fail(resp, e);
                    auto result = extension_.remove_map_subnet(
                        static_cast<std::uint8_t>(req.net()));
                    if (!result.error.empty()) return fail(resp, result.error);
                    resp.set_success(true);
                    resp.set_removed(result.removed);
                });
        }
        if (method == "ListMap") {
            return handle<AunListMapRequest, AunListMapResponse>(
                method, request, response, [&](const auto&, auto& resp) {
                    auto listing = extension_.list_map();
                    resp.set_error(listing.error);
                    for (const auto& p : listing.peers) {
                        auto* entry = resp.add_peers();
                        entry->set_net(p.net);
                        entry->set_stn(p.stn);
                        entry->set_host(p.host);
                        entry->set_port(p.port);
                        entry->set_label(p.label);
                        entry->set_resolved(p.resolved);
                        entry->set_resolved_ip(p.resolved_ip);
                    }
                    for (const auto& s : listing.subnets) {
                        auto* entry = resp.add_subnets();
                        entry->set_net(s.net);
                        entry->set_subnet(s.subnet);
                        entry->set_label(s.label);
                    }
                });
        }
        return RpcStatus::error(
            kRpcUnimplemented,
            "AunService has no method '" + std::string(method) + "'");
    }

private:
    // Parse the request, run `body` under the lock, serialize the response.
    template <typename Req, typename Resp, typename Body>
    RpcStatus handle(std::string_view method, std::string_view request,
                     std::string& response, Body&& body) {
        Req req;
        if (!req.ParseFromArray(request.data(),
                                static_cast<int>(request.size()))) {
            return RpcStatus::error(
                kRpcInvalidArgument,
                "malformed AunService." + std::string(method) + " request");
        }
        Resp resp;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            body(req, resp);
        }
        return serialized(resp.SerializeToString(&response));
    }

    template <typename Resp>
    static void fail(Resp& resp, const std::string& error) {
        resp.set_success(false);
        resp.set_error(error);
    }

    // Range checks for the numeric fields before they are narrowed to a byte or
    // a port (#168). Each returns an empty string when the value is in range, or
    // a field-named message otherwise. net is the full Econet net byte (0-255),
    // stn an addressable station (1-254), port a usable UDP port (1-65535).
    static std::string check_net(std::uint32_t net) {
        return net > 255 ? "net must be 0-255" : std::string{};
    }
    static std::string check_stn(std::uint32_t stn) {
        return (stn < 1 || stn > 254) ? "stn must be 1-254" : std::string{};
    }
    static std::string check_port(std::uint32_t port) {
        return (port < 1 || port > 65535) ? "port must be 1-65535"
                                          : std::string{};
    }

    void add_peer(const AunAddPeerRequest& req, AunAddPeerResponse& resp) {
        // Validate against the documented range (net 0..255: the full Econet
        // net byte, which a guest can address). Edits the desired peer set
        // whether or not a backend is up.
        if (auto e = check_net(req.net()); !e.empty()) {
            return fail(resp, e);
        }
        if (auto e = check_stn(req.stn()); !e.empty()) {
            return fail(resp, e);
        }
        // port 0 is the documented "use the AUN default" sentinel here (unlike
        // the map RPCs, where a concrete port is required); any other value
        // must still be a real port, not silently truncated (#168).
        if (req.port() != 0) {
            if (auto e = check_port(req.port()); !e.empty()) {
                return fail(resp, e);
            }
        }
        in_addr addr{};
        if (inet_pton(AF_INET, req.ip_address().c_str(), &addr) != 1) {
            return fail(resp, "invalid ip_address");
        }
        std::uint16_t port = (req.port() == 0)
            ? AUN_DEFAULT_PORT
            : static_cast<std::uint16_t>(req.port());
        extension_.add_api_peer(static_cast<std::uint8_t>(req.net()),
                                static_cast<std::uint8_t>(req.stn()),
                                addr.s_addr, port);
        resp.set_success(true);
    }

    static AunPeerSource to_proto_source(AunPeerProvenance provenance) {
        switch (provenance) {
            case AunPeerProvenance::Launch:     return AUN_PEER_SOURCE_LAUNCH;
            case AunPeerProvenance::Api:        return AUN_PEER_SOURCE_API;
            case AunPeerProvenance::MapFile:    return AUN_PEER_SOURCE_MAP_FILE;
            case AunPeerProvenance::Discovered: return AUN_PEER_SOURCE_DISCOVERED;
            case AunPeerProvenance::Subnet:     return AUN_PEER_SOURCE_SUBNET;
        }
        return AUN_PEER_SOURCE_UNSPECIFIED;
    }

    void list_peers(AunListPeersResponse& resp) {
        for (const auto& info : extension_.peer_set().list_peers()) {
            auto* peer = resp.add_peers();
            peer->set_net(info.net);
            peer->set_stn(info.stn);
            peer->set_ip_address(ip_to_dotted(info.ip_addr));
            peer->set_port(info.port);
            peer->set_source(to_proto_source(info.provenance));
        }
    }

    static std::string ip_to_dotted(std::uint32_t ip_net_byte_order) {
        char buf[INET_ADDRSTRLEN];
        in_addr addr;
        addr.s_addr = ip_net_byte_order;
        if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
            return buf;
        }
        return {};
    }

    AunEconetTransportExtension& extension_;
    std::mutex mutex_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_DISPATCHER_HPP

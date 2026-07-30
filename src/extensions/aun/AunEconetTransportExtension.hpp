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

#ifndef BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP
#define BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP

// Built-in EconetTransportExtension that wraps AunBackend (the existing
// UDP/AUN transport). Reads its configuration from the framework-managed
// config map populated from --aun port=...:map=... (CLI) or
// econet.transport.parameters (preset). Constructs and returns an
// AunBackend on demand; owns no machine state of its own.
//
// Manifest is constructed by builtin_extensions::entries() and includes
// two parameters:
//   port (string, default "32768"): UDP port to bind. Special value
//                                   "none" disables the network (no
//                                   socket bound; create_backend returns
//                                   nullptr).
//   map  (string, is_list):         Repeated peer entries of the form
//                                   "net.stn@ip@port". Each --aun
//                                   map=... or preset JSON array element
//                                   contributes one PeerSpec. '@' is used
//                                   as the inner separator because it is
//                                   shell-safe across bash/zsh/fish/cmd/
//                                   PowerShell and does not collide with
//                                   '.' (inside net.stn and IPv4) or ':'
//                                   (the top-level arg separator).

#include "AunUi.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace beebium {

class ExtensionRpcDispatcher;
class AunDispatcher;           // forward; defined when service is built
class AunDiscoveryAnnouncer;   // forward; defined in this dir
class AunDiscoverySubscriber;  // forward; defined in this dir

class AunEconetTransportExtension : public EconetTransportExtension {
public:
    // Parameter parsing for an individual map= entry.
    struct PeerSpec {
        std::uint8_t net;
        std::uint8_t stn;
        std::uint32_t ip_addr_net_byte_order;
        std::uint16_t port;
    };

    AunEconetTransportExtension();
    ~AunEconetTransportExtension() override;

    // Construct an AunBackend from the current config. Returns nullptr if
    // port=none (network disabled) or if the underlying socket bind
    // fails. The caller (ServerMain) treats nullptr as "AUN configured
    // but transport unavailable" -- the EconetSocket is still constructed
    // in disconnected state.
    //
    // The constructed backend is also stashed as a non-owning pointer so
    // AunService can manipulate it. The owning unique_ptr is handed off
    // to EconetSocket; the extension does not extend the backend's
    // lifetime and the raw pointer becomes dangling once the machine
    // shuts down (which only happens at process exit).
    std::unique_ptr<NetworkBackend> create_backend(std::uint8_t station) override;

    // AUN-specific operations (peer table, cable plug, port status), served
    // through the core's ExtensionRpc channel rather than a hosted gRPC service.
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // Non-owning pointer to the AunBackend we constructed. Returns
    // nullptr before create_backend has run or if construction failed
    // (port=none, bind error). AunService consults this to find its
    // backend on each RPC; AunUi reads from it to populate the View.
    AunBackend* backend() { return backend_; }
    const AunBackend* backend() const { return backend_; }

    // ExtensionUi hook: returns a stable pointer to the per-extension
    // AunUi. The framework reads its View tree and dispatches validated
    // events into its handle_event.
    ExtensionUi* ui() override { return &ui_; }

    // Helpers exposed for unit testing -- these are pure functions of the
    // config map and don't touch sockets.

    // Parse the "port" config value. "none" -> std::nullopt. Empty or
    // missing -> AUN_DEFAULT_PORT. Otherwise parsed as decimal uint16.
    static std::optional<std::uint16_t> parse_port(const std::string& value);

    // Parse the "net" config value. Empty or missing -> 0 (matches the
    // historical default before this parameter existed). Otherwise
    // parsed as decimal in the range 0..127 -- the high bit of an
    // Econet net byte is reserved by the Acorn bridge protocol. Invalid
    // input falls back to 0 with a warning to stderr.
    static std::uint8_t parse_net(const std::string& value);

    // Parse a list of "map" entries. Each element is one peer of the
    // form "[net.]stn@ip@port". Entries that can't be parsed are
    // dropped with a warning to stderr.
    static std::vector<PeerSpec> parse_map(std::span<const std::string> entries);

private:
    AunBackend* backend_ = nullptr;  // non-owning; lives in EconetSocket
    std::unique_ptr<AunDispatcher> dispatcher_;  // lazily constructed
    // Owned by the extension so its lifetime ends with the extension.
    // The backend lives inside EconetSocket and outlives us: ServerMain
    // declares the machine before the transport registry precisely so
    // that reverse destruction order stops these collaborators -- which
    // hold a reference to that backend and run callbacks on the mDNS
    // browser thread -- before the backend itself goes away. nullptr
    // until create_backend() succeeds; nullptr on platforms without
    // mDNS or when the announcer fails to start.
    std::unique_ptr<AunDiscoveryAnnouncer> announcer_;
    // Subscribes to peer announcements and feeds them into the
    // backend. Same lifetime model as announcer_.
    std::unique_ptr<AunDiscoverySubscriber> subscriber_;
    AunUi ui_{*this};
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP

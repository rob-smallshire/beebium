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
#include "AunMapWriter.hpp"
#include "beebium/econet/AunPacket.hpp"
#include "beebium/econet/AutoStationState.hpp"
#include "beebium/net/SocketPlatform.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "AunDispatcher.hpp"
#endif

#ifndef BEEBIUM_VERSION
#define BEEBIUM_VERSION "unknown"
#endif

#include "beebium/PlatformUtils.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#endif

#include <cctype>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <tuple>

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

// Resolve a host (IPv4 literal or DNS name) to an IPv4 address in network byte
// order. A literal returns at once. A name runs getaddrinfo on a detached
// thread bounded by `timeout`: if it does not answer in time the host is
// treated as unresolved for this pass (the detached lookup finishes and its
// result is discarded through the shared promise), so a slow resolver never
// stalls the caller -- which runs on create_backend or the sweep thread, never
// the emulation thread.
std::optional<std::uint32_t> resolve_host_bounded(
        const std::string& host, std::chrono::milliseconds timeout) {
    in_addr literal{};
    if (inet_pton(AF_INET, host.c_str(), &literal) == 1) {
        return literal.s_addr;
    }
    auto promise =
        std::make_shared<std::promise<std::optional<std::uint32_t>>>();
    auto future = promise->get_future();
    std::thread([host, promise]() {
        // getaddrinfo needs Winsock initialised on Windows. The normal server
        // path binds an AunBackend first (which does it), but the aun-map
        // subcommands and a reload resolve here with no bound backend, so do it
        // ourselves -- idempotent and refcounted (#149 Windows verification).
        beebium::net::ensure_winsock_initialized();
        std::optional<std::uint32_t> result;
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 &&
            res != nullptr) {
            result = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
        }
        if (res != nullptr) {
            ::freeaddrinfo(res);
        }
        promise->set_value(result);
    }).detach();
    if (future.wait_for(timeout) == std::future_status::ready) {
        return future.get();
    }
    return std::nullopt;  // did not resolve in time; retried on the next reload
}

// Format an IPv4 address (network byte order) as a dotted quad.
std::string ip_to_dotted(std::uint32_t ip_net_byte_order) {
    in_addr addr{};
    addr.s_addr = ip_net_byte_order;
    char buf[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
        return buf;
    }
    return {};
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
    if (!parse_uint(value, parsed) || parsed > 255) {
        std::cerr << "AUN extension: invalid net '" << value
                  << "' (expected 0..255) -- using default 0\n";
        return 0;
    }
    return static_cast<std::uint8_t>(parsed);
}

std::optional<AunEconetTransportExtension::DiscoveryMode>
AunEconetTransportExtension::parse_discovery_mode(const std::string& value) {
    std::string v;
    v.reserve(value.size());
    for (char c : value) {
        v.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    if (v.empty() || v == "on") return DiscoveryMode::On;
    if (v == "announce") return DiscoveryMode::Announce;
    if (v == "browse") return DiscoveryMode::Browse;
    if (v == "off") return DiscoveryMode::Off;
    return std::nullopt;
}

std::string AunEconetTransportExtension::discovery_mode_name(DiscoveryMode mode) {
    switch (mode) {
        case DiscoveryMode::On: return "on";
        case DiscoveryMode::Announce: return "announce";
        case DiscoveryMode::Browse: return "browse";
        case DiscoveryMode::Off: return "off";
    }
    return "on";
}

std::optional<std::string> AunEconetTransportExtension::config_error() const {
    if (auto value = config_value("discovery")) {
        if (!parse_discovery_mode(std::string(*value)).has_value()) {
            return "discovery must be one of off, announce, browse, on (got '" +
                   std::string(*value) + "')";
        }
    }
    return std::nullopt;
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

namespace {

struct SubnetSpec {
    std::uint8_t net;
    std::uint32_t base_ip;  // network byte order, /24 network address
};

// Parse --aun subnet= entries of the form "net@a.b.c.0/24". Only /24 is
// supported. Malformed entries are dropped with a warning to stderr.
std::vector<SubnetSpec> parse_subnet_specs(
        std::span<const std::string> entries) {
    std::vector<SubnetSpec> specs;
    for (const auto& entry : entries) {
        if (entry.empty()) continue;
        auto fields = split_on(entry, '@');
        if (fields.size() != 2) {
            std::cerr << "AUN extension: malformed subnet entry '" << entry
                      << "' (expected net@a.b.c.0/24) -- skipping\n";
            continue;
        }
        unsigned long net = 0;
        if (!parse_uint(fields[0], net) || net > 255) {
            std::cerr << "AUN extension: invalid net '" << fields[0]
                      << "' in subnet entry -- skipping\n";
            continue;
        }
        auto slash = fields[1].find('/');
        if (slash == std::string::npos || fields[1].substr(slash + 1) != "24") {
            std::cerr << "AUN extension: subnet '" << fields[1]
                      << "' must be a /24 -- skipping\n";
            continue;
        }
        in_addr addr{};
        if (inet_pton(AF_INET, fields[1].substr(0, slash).c_str(), &addr) != 1) {
            std::cerr << "AUN extension: invalid subnet address '" << fields[1]
                      << "' -- skipping\n";
            continue;
        }
        specs.push_back(
            SubnetSpec{static_cast<std::uint8_t>(net),
                       addr.s_addr & htonl(0xFFFFFF00u)});
    }
    return specs;
}

}  // namespace

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

    // Re-seed the launch-scoped layers from the current config (Launch from
    // --aun map=/subnet=, MapFile from the per-user file), clearing Discovered
    // so a fresh subscriber repopulates it. The Api layer persists across
    // backend recreation -- a peer added via AunService.AddPeer before the
    // socket came up, or before a reconnect, survives and is applied here. The
    // same seeding feeds select_auto_station's occupied-station view.
    seed_operator_layers(local_net);

    backend_ = backend.get();  // non-owning; ownership goes to EconetSocket

    // Bind the peer set to the live socket and push the whole resolved routing
    // view (Launch + any Api/MapFile already present) to it, then apply the
    // desired cable state recorded by any SetConnected that arrived early.
    peer_set_.attach(backend_);
    backend_->set_connected(desired_connected_);

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

    // Discovery mode (#158): whether we publish and/or browse _aun._udp.
    auto discovery_value = config_value("discovery");
    discovery_mode_ = parse_discovery_mode(
        discovery_value ? std::string(*discovery_value) : std::string{})
                          .value_or(DiscoveryMode::On);
    const bool do_announce = discovery_mode_ == DiscoveryMode::On ||
                             discovery_mode_ == DiscoveryMode::Announce;
    const bool do_browse = discovery_mode_ == DiscoveryMode::On ||
                           discovery_mode_ == DiscoveryMode::Browse;

    // Publish a DNS-SD announcement so other AUN-capable peers can
    // discover us without an explicit --aun map= entry (unless discovery is
    // browse/off, which do not publish). Failure to start (e.g. mDNS
    // unavailable on this platform) is non-fatal -- the transport still works
    // for explicitly-mapped peers.
    //
    // machine_uuid is injected by ServerMain so the announcement's
    // impl-identity TXT record matches the same UUID this server
    // publishes on its _beebium._tcp announcement -- a discovering
    // tool can correlate the two and know which AUN peer is which
    // Beebium machine.
    if (do_announce) {
        announcer_ = std::make_unique<AunDiscoveryAnnouncer>(
            local_net, station, backend->local_port(),
            std::string{"beebium"}, std::string{BEEBIUM_VERSION},
            machine_uuid);  // copied; the subscriber needs it too, below
        if (!discovery_service_type_.empty()) {
            announcer_->set_service_type(discovery_service_type_);
        }
        if (!announcer_->start()) {
            std::cerr << "AUN extension: mDNS announcement unavailable -- "
                         "discovery disabled\n";
        }
    }

    // Subscribe to peer announcements published by other AUN-capable
    // stations and add them to our peer table as Discovered entries (unless
    // discovery is announce/off, which do not browse). Operator-configured
    // peers (--aun map= or AunService::AddPeer) take precedence over
    // discovered ones -- see AunBackend::add_peer's PeerSource handling.
    if (do_browse) {
        subscriber_ = std::make_unique<AunDiscoverySubscriber>(
            peer_set_, station, nullptr, std::move(machine_uuid));
        if (!discovery_service_type_.empty()) {
            subscriber_->set_service_type(discovery_service_type_);
        }
        // Share the announcer's bind-time "since" (when we also announce) so
        // both halves agree which of two instances racing for one number is
        // the incumbent (#147); browse-only keeps the subscriber's own
        // construction-time since.
        if (announcer_) {
            subscriber_->set_own_since(announcer_->since());
        }
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
    }

    // React to a runtime station change (EconetService::SetStationId, on a gRPC
    // thread): re-announce with the new station so peers stop seeing the stale
    // one, and update the subscriber's self-filter so this machine now accepts
    // the old station number as a peer. The announcer's start() is idempotent
    // (it withdraws the old station's announcement and publishes the new one).
    //
    // The callback captures a weak liveness token rather than requiring the
    // extension to detach it at teardown: destruction order between this
    // extension and the backend differs between production (machine outlives
    // the transport registry) and the unit tests (the returned backend is a
    // local destroyed first). If the token has expired the extension is gone
    // and the callback does nothing. The gRPC server is stopped before either
    // is destroyed, so no callback is ever in flight during teardown.
    callback_alive_ = std::make_shared<bool>(true);
    std::weak_ptr<bool> alive = callback_alive_;
    backend_->set_station_changed_callback(
        [this, alive](std::uint8_t new_station) {
            auto keep_alive = alive.lock();
            if (!keep_alive) return;
            std::lock_guard<std::mutex> lock(discovery_mutex_);
            // A station change is a fresh claim, so stamp a new "since" and
            // give both halves the same value, re-running the incumbent-vs-
            // newcomer decision from this moment (#147).
            const std::int64_t since_now =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            if (announcer_) {
                announcer_->set_since(since_now);
                announcer_->set_local_station(new_station);
                announcer_->start();
            }
            if (subscriber_) {
                subscriber_->set_own_since(since_now);
                subscriber_->set_local_station(new_station);
            }
        });

    // Poll the map file for edits on the subscriber's periodic sweep (off the
    // emulation thread). The weak token makes a sweep during teardown a no-op.
    if (subscriber_) {
        subscriber_->set_on_sweep([this, alive]() {
            auto keep_alive = alive.lock();
            if (!keep_alive) return;
            poll_map_file();
        });
    }

    // React to the backend being freed (EconetService::DisableEconet drops
    // EconetSocket's reference; nothing else notifies us). Drop our raw pointer
    // and detach the peer set FIRST, so any discovery write still in flight
    // becomes a no-op rather than a use-after-free, then stop announcing and
    // browsing -- Disable means off the network. The weak token guards against
    // the extension having been destroyed first (machine teardown), and the
    // backend-identity check guards against a late, reader-deferred destruction
    // of an OLD backend clobbering a NEW one created by a re-Enable: we act only
    // while the backend being destroyed is still the one we hold. A later
    // create_backend re-seeds Launch/Discovered, re-attaches, and re-announces,
    // keeping the Api layer (so an added peer is routed again after re-Enable).
    AunBackend* released = backend_;
    backend_->set_destroyed_callback([this, alive, released]() {
        auto keep_alive = alive.lock();
        if (!keep_alive) return;
        std::lock_guard<std::mutex> lock(discovery_mutex_);
        if (backend_ != released) return;  // already replaced by a re-Enable
        peer_set_.attach(nullptr);  // future applies no-op; closes the UAF
        backend_ = nullptr;
        subscriber_.reset();  // stop browsing + join the sweep thread
        announcer_.reset();   // stop advertising a station with no backend
    });
    return backend;
}

void AunEconetTransportExtension::seed_operator_layers(std::uint8_t local_net) {
    // Launch from --aun map= / the preset, cleared and re-seeded; Discovered and
    // the launch Subnet rules cleared so a fresh subscriber repopulates them.
    // The Api layer persists across recreation.
    peer_set_.set_local_net(local_net);
    peer_set_.clear_provenance(AunPeerProvenance::Launch);
    peer_set_.clear_provenance(AunPeerProvenance::Discovered);
    peer_set_.clear_provenance(AunPeerProvenance::Subnet);
    peer_set_.clear_subnet_rules(AunPeerProvenance::Launch);
    auto map_entries = config_list("map");
    for (const auto& p :
         parse_map(map_entries ? *map_entries : std::span<const std::string>{})) {
        peer_set_.set_peer(p.net, p.stn, p.ip_addr_net_byte_order, p.port,
                           AunPeerProvenance::Launch);
    }
    auto subnet_entries = config_list("subnet");
    for (const auto& s : parse_subnet_specs(
             subnet_entries ? *subnet_entries
                            : std::span<const std::string>{})) {
        peer_set_.set_subnet_rule(s.net, s.base_ip, AunPeerProvenance::Launch);
    }
    // Load the per-user map file (MapFile peers + subnet rules), off the
    // emulation thread. A malformed or missing file is reported, never fatal.
    resolve_map_file_path();
    reload_map_file();
}

EconetTransportExtension::AutoStationOutcome
AunEconetTransportExtension::select_auto_station(econet::StationRange range) {
    using Status = AutoStationOutcome::Status;
    AutoStationOutcome out;

    // Env overrides for the timings, so an integration test can force a long
    // browse (to prove the gRPC port is printed before selection finishes)
    // without a code hook. Milliseconds; ignored if unset or unparseable.
    auto env_ms = [](const char* name, std::chrono::milliseconds& target) {
        if (const char* v = std::getenv(name)) {
            char* end = nullptr;
            long ms = std::strtol(v, &end, 10);
            if (end != v && ms >= 0) {
                target = std::chrono::milliseconds(ms);
            }
        }
    };
    env_ms("BEEBIUM_AUN_AUTO_MIN_OBSERVE_MS", auto_min_observe_);
    env_ms("BEEBIUM_AUN_AUTO_QUIET_MS", auto_quiet_);
    env_ms("BEEBIUM_AUN_AUTO_BUDGET_MS", auto_budget_);

    // port=none means the transport is disabled -- there is nothing to select
    // for. Report Unsupported so the launch path treats it like Piconet.
    auto port_value = config_value("port");
    auto port = parse_port(port_value ? std::string(*port_value) : std::string{});
    if (!port.has_value()) {
        out.status = Status::Unsupported;
        out.report = "AUN transport is disabled (port=none); --station auto needs "
                     "a port to claim a number on";
        return out;
    }

    auto net_value = config_value("net");
    std::uint8_t local_net =
        parse_net(net_value ? std::string(*net_value) : std::string{});

    // Seed the operator layers so map= and map-file peers count as occupied,
    // then browse briefly so discovered peers do too.
    seed_operator_layers(local_net);

    auto machine_uuid_value = config_value("machine_uuid");
    std::string machine_uuid = machine_uuid_value
                                   ? std::string(*machine_uuid_value)
                                   : std::string{};

    // Monotonic allocation hint (issue #161): begin the search one past the
    // last number this host allocated (wrapping), so a freed number is not
    // reused immediately. The file is only a spacing hint; the claim/settle
    // below stays the authority. BEEBIUM_AUN_AUTO_STATE_FILEPATH overrides the
    // path and `none` disables it; any failure falls back to lowest-free (start
    // at range.lo) with at most one warning, never failing the launch.
    std::uint8_t search_start = range.lo;
    std::optional<std::filesystem::path> state_filepath;
    {
        auto env = beebium::platform::get_env("BEEBIUM_AUN_AUTO_STATE_FILEPATH");
        if (!(env && *env == "none")) {
            state_filepath = env
                ? std::filesystem::path(*env)
                : beebium::platform::user_state_base_dirpath() / "aun-auto-next";
            auto chosen_start = econet::advance_auto_station_start(
                *state_filepath, range, std::chrono::milliseconds(300));
            if (chosen_start.has_value()) {
                search_start = *chosen_start;
            } else {
                std::cerr << "AUN: auto-station hint file unavailable ("
                          << state_filepath->string()
                          << "); using the lowest free number\n";
                state_filepath.reset();  // nothing to record at the end
            }
        }
    }

    // Discovery mode (#158) governs how selection runs:
    //   On       -- browse AND claim: observe, claim, and arbitrate a
    //               same-instant race by climbing past an earlier-bound
    //               incumbent (#147).
    //   Browse   -- browse but do not claim: observe discovered peers, then take
    //               the lowest free number. A same-instant race is NOT
    //               arbitrated (two browse-only instances may pick one number).
    //   Announce -- publish but do not browse: we cannot see others, so the
    //               in-use set is operator-only (map / map-file / launch) as in
    //               Off; the claim itself is published later by create_backend.
    //   Off      -- neither: operator-only in-use set, resolved immediately.
    // The per-host hint (search_start, #161) applies in every mode.
    auto discovery_value = config_value("discovery");
    discovery_mode_ = parse_discovery_mode(
        discovery_value ? std::string(*discovery_value) : std::string{})
                          .value_or(DiscoveryMode::On);
    const bool do_browse = discovery_mode_ == DiscoveryMode::On ||
                           discovery_mode_ == DiscoveryMode::Browse;
    const bool do_claim = discovery_mode_ == DiscoveryMode::On;

    auto occupied_now = [&](std::set<std::uint8_t> extra) {
        for (const auto& e : peer_set_.list_peers()) {
            if (e.net == local_net && e.provenance != AunPeerProvenance::Subnet) {
                extra.insert(e.stn);
            }
        }
        return extra;
    };

    // Record the taken number in the hint file (best-effort) and build the
    // outcome. nullopt means the range was exhausted (start at range.lo).
    auto finalise = [&](std::optional<std::uint8_t> chosen) {
        if (!chosen.has_value()) {
            out.status = Status::Exhausted;
            out.station = range.lo;
            out.report = "Econet: all station numbers " + std::to_string(range.lo) +
                         "-" + std::to_string(range.hi) +
                         " are in use; starting at " + std::to_string(range.lo);
            return out;
        }
        out.status = Status::Selected;
        out.station = *chosen;
        if (state_filepath.has_value()) {
            econet::record_auto_station(*state_filepath, *chosen,
                                        std::chrono::milliseconds(300));
        }
        return out;
    };

    if (!do_browse) {
        // Announce / Off: no observation; operator-only in-use set; immediate.
        return finalise(econet::lowest_free_station_from(occupied_now({}), range,
                                                         search_start));
    }

    // We browse. Build the subscriber (the claim announcer, when we claim,
    // advertises a non-zero port even for an OS-ephemeral port=0: peers resolve
    // the SRV record to detect a collision and a zero SRV port never completes
    // that resolve. The real port is advertised by create_backend right after.)
    const std::uint16_t claim_port = (*port != 0) ? *port : AUN_DEFAULT_PORT;
    auto subscriber = std::make_unique<AunDiscoverySubscriber>(
        peer_set_, /*local_stn=*/0, nullptr, machine_uuid);
    if (!discovery_service_type_.empty()) {
        subscriber->set_service_type(discovery_service_type_);
    }

    if (!do_claim) {
        // Browse-only: observe for the minimum window, then take the lowest free
        // number with discovered peers counted. No claim, so a same-instant race
        // is not arbitrated (documented).
        subscriber->start();
        std::this_thread::sleep_for(auto_min_observe_);
        auto chosen = econet::lowest_free_station_from(occupied_now({}), range,
                                                       search_start);
        subscriber->stop();
        return finalise(chosen);
    }

    // On: browse AND claim. Keep a claim up continuously and poll, climbing past
    // an earlier-bound incumbent so instances launched together settle on
    // distinct numbers.
    auto announcer = std::make_unique<AunDiscoveryAnnouncer>(
        local_net, range.lo, claim_port, std::string{"beebium"},
        std::string{BEEBIUM_VERSION}, machine_uuid);
    if (!discovery_service_type_.empty()) {
        announcer->set_service_type(discovery_service_type_);
    }

    std::set<std::uint8_t> tried;  // numbers an incumbent pushed us off
    auto first_free =
        econet::lowest_free_station_from(occupied_now({}), range, search_start);
    std::uint8_t candidate = first_free.value_or(search_start);
    bool exhausted = !first_free.has_value();

    announcer->set_local_station(candidate);
    announcer->start();
    subscriber->set_own_since(announcer->since());
    subscriber->set_local_station(candidate);
    subscriber->start();

    const auto start = std::chrono::steady_clock::now();
    auto last_move = start;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto now = std::chrono::steady_clock::now();
        if (now - start >= auto_budget_) {
            break;  // hard cap; keep whatever candidate we hold
        }
        if (subscriber->own_number_contested_as_newcomer()) {
            // Someone holds this number and bound before us: climb to the next
            // free one, re-announce there, and restart the quiet timer.
            tried.insert(candidate);
            auto next = econet::lowest_free_station_from(occupied_now(tried),
                                                         range, search_start);
            if (!next.has_value()) {
                exhausted = true;
                break;  // whole range taken; keep the current candidate
            }
            // Keep our original bind-time "since": a stable, launch-ordered
            // tie-break so the earlier-launched instance wins a shared number.
            candidate = *next;
            announcer->set_local_station(candidate);
            announcer->start();
            subscriber->set_own_since(announcer->since());
            subscriber->set_local_station(candidate);
            last_move = now;
            continue;
        }
        // Settle once the candidate has stood unchallenged for the quiet period
        // AND we have observed long enough for a pre-existing peer to surface.
        if (now - start >= auto_min_observe_ && now - last_move >= auto_quiet_) {
            break;
        }
    }
    subscriber->stop();
    announcer->stop();
    return finalise(exhausted ? std::optional<std::uint8_t>{}
                              : std::optional<std::uint8_t>(candidate));
}

AunEconetTransportExtension::AunEconetTransportExtension() = default;
AunEconetTransportExtension::~AunEconetTransportExtension() = default;

void AunEconetTransportExtension::add_api_peer(
        std::uint8_t net, std::uint8_t stn,
        std::uint32_t ip_addr_net_byte_order, std::uint16_t port) {
    // Edits the Api layer of the peer set regardless of whether a backend is
    // up; if one is attached the resolved routing view is applied immediately.
    peer_set_.set_peer(net, stn, ip_addr_net_byte_order, port,
                       AunPeerProvenance::Api);
}

void AunEconetTransportExtension::remove_api_peer(std::uint8_t net,
                                                  std::uint8_t stn) {
    peer_set_.remove_peer(net, stn, AunPeerProvenance::Api);
}

bool AunEconetTransportExtension::set_desired_connected(bool connected) {
    desired_connected_ = connected;
    if (backend_ != nullptr) {
        backend_->set_connected(connected);
        return true;
    }
    return false;  // remembered; applied at the next create_backend
}

void AunEconetTransportExtension::resolve_map_file_path() {
    // Precedence: --aun map-file= > BEEBIUM_AUN_MAP_FILEPATH > the shared
    // per-user default. "none" (from the CLI) disables the map file entirely.
    bool enabled = true;
    std::string path;
    if (auto cfg = config_value("map-file")) {
        std::string value(*cfg);
        if (value == "none") {
            enabled = false;
        } else if (!value.empty()) {
            path = value;
        }
    }
    if (enabled && path.empty()) {
        if (auto env = beebium::platform::get_env("BEEBIUM_AUN_MAP_FILEPATH");
            env && !env->empty()) {
            path = *env;
        }
    }
    if (enabled && path.empty()) {
        path = (beebium::platform::user_state_base_dirpath() / "aun-map.json")
                   .string();
    }
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    map_file_enabled_ = enabled;
    map_file_filepath_ = enabled ? path : std::string{};
}

AunEconetTransportExtension::ReloadResult
AunEconetTransportExtension::reload_map_file() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) {
            return {false, ""};
        }
        path = map_file_filepath_;
    }

    AunMapLoadResult loaded = load_aun_map(path);
    if (!loaded.map.has_value()) {
        // Present but malformed: keep the previous MapFile entries so a bad edit
        // does not drop a working table, and report the error.
        std::cerr << "AUN extension: map file error: " << loaded.error << "\n";
        {
            std::lock_guard<std::mutex> lock(map_file_mutex_);
            map_file_error_ = loaded.error;
        }
        // The load error is new state the sidebar shows (the FileReference goes
        // ERROR), so re-push the view even though the peer table is unchanged.
        ui_.mark_dirty();
        return {true, loaded.error};
    }

    // Resolve hostnames off any lock (bounded, so a slow resolver cannot stall
    // the sweep). Unresolved hosts are kept for display rather than routed.
    std::vector<std::tuple<std::uint8_t, std::uint8_t, std::uint32_t,
                           std::uint16_t>>
        resolved;
    std::vector<AunMapPeer> unreachable;
    for (const auto& peer : loaded.map->peers) {
        if (auto ip = resolve_host_bounded(peer.host, std::chrono::seconds(3))) {
            resolved.emplace_back(peer.net, peer.stn, *ip, peer.port);
        } else {
            unreachable.push_back(peer);
        }
    }

    // Replace only the MapFile peer layer and the MapFile and Subnet rules;
    // Api, Launch and Discovered are untouched. Materialised Subnet peers are
    // cleared too, since the rules may have changed; they re-materialise.
    peer_set_.clear_provenance(AunPeerProvenance::MapFile);
    peer_set_.clear_provenance(AunPeerProvenance::Subnet);
    peer_set_.clear_subnet_rules(AunPeerProvenance::MapFile);
    for (const auto& [net, stn, ip, port] : resolved) {
        peer_set_.set_peer(net, stn, ip, port, AunPeerProvenance::MapFile);
    }
    for (const auto& subnet : loaded.map->subnets) {
        peer_set_.set_subnet_rule(subnet.net, subnet.base_ip,
                                  AunPeerProvenance::MapFile);
    }

    std::uint32_t entry_count =
        static_cast<std::uint32_t>(resolved.size() + unreachable.size() +
                                   loaded.map->subnets.size());
    // Record the file's modification time so the sweep poll can tell when it
    // next changes; min() stands for an absent file.
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        map_file_error_.clear();
        map_file_entry_count_ = entry_count;
        unreachable_map_peers_ = std::move(unreachable);
        // Cache the file's peers (with labels) and subnets so the sidebar can
        // show labels and the subnet-rules group cheaply, without re-reading or
        // re-resolving on every build_view.
        map_peers_ = loaded.map->peers;
        map_subnets_ = loaded.map->subnets;
        map_file_mtime_ =
            ec ? std::filesystem::file_time_type::min() : mtime;
    }
    // A live subscriber re-checks its discovered peers against the new file so a
    // file-vs-discovered disagreement raises or clears as the file changes.
    if (subscriber_) {
        subscriber_->revalidate_file_disagreements();
    }
    // Re-push the AUN panel. A reload is the single funnel for every map-file
    // change -- the sweep poll, ReloadMap, each edit RPC, a subcommand write
    // another instance made, a host that resolves on a later pass -- and
    // before this the view was only re-pushed when discovery changed, so a
    // poll-driven reload left a second instance's sidebar stale until its own
    // Reload (issue #146). The peer rows, the list-title counts and the
    // FileReference state all rebuild from this on the next ExtensionUi poll.
    ui_.mark_dirty();
    return {true, ""};
}

void AunEconetTransportExtension::poll_map_file() {
    std::string path;
    std::filesystem::file_time_type last;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) return;
        path = map_file_filepath_;
        last = map_file_mtime_;
    }
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    auto current = ec ? std::filesystem::file_time_type::min() : mtime;
    if (current != last) {
        reload_map_file();  // records the new mtime
    }
}

std::string AunEconetTransportExtension::map_file_path() const {
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    return map_file_filepath_;
}

std::uint32_t AunEconetTransportExtension::map_file_entry_count() const {
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    return map_file_entry_count_;
}

std::string AunEconetTransportExtension::map_file_error() const {
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    return map_file_error_;
}

std::vector<AunMapPeer> AunEconetTransportExtension::unreachable_map_peers() const {
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    return unreachable_map_peers_;
}

std::vector<AunMapPeer> AunEconetTransportExtension::map_peers() const {
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    return map_peers_;
}

std::vector<AunMapSubnet> AunEconetTransportExtension::map_subnets() const {
    std::lock_guard<std::mutex> lock(map_file_mutex_);
    return map_subnets_;
}

AunEconetTransportExtension::MapEdit AunEconetTransportExtension::add_map_peer(
        std::uint8_t net, std::uint8_t stn, const std::string& host,
        std::uint16_t port, const std::string& label) {
    resolve_map_file_path();
    std::string path;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) return {"the map file is disabled (map-file=none)"};
        path = map_file_filepath_;
    }
    auto load = AunMapDocument::load(path);
    if (!load.document) return {load.error};
    auto document = *load.document;
    if (std::string err = document.add_or_replace_peer(net, stn, host, port, label);
        !err.empty()) {
        return {err};
    }
    if (std::string err = document.save(path); !err.empty()) {
        return {err};
    }
    reload_map_file();  // apply the server's own write to its peer set at once
    return {"", true};
}

AunEconetTransportExtension::MapEdit AunEconetTransportExtension::remove_map_peer(
        std::uint8_t net, std::uint8_t stn) {
    resolve_map_file_path();
    std::string path;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) return {"the map file is disabled (map-file=none)"};
        path = map_file_filepath_;
    }
    auto load = AunMapDocument::load(path);
    if (!load.document) return {load.error};
    auto document = *load.document;
    bool removed = document.remove_peer(net, stn);
    if (removed) {
        if (std::string err = document.save(path); !err.empty()) {
            return {err};
        }
        reload_map_file();
    }
    return {"", removed};
}

AunEconetTransportExtension::MapEdit AunEconetTransportExtension::add_map_subnet(
        std::uint8_t net, const std::string& subnet_text,
        const std::string& label) {
    resolve_map_file_path();
    std::string path;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) return {"the map file is disabled (map-file=none)"};
        path = map_file_filepath_;
    }
    auto load = AunMapDocument::load(path);
    if (!load.document) return {load.error};
    auto document = *load.document;
    if (std::string err = document.add_or_replace_subnet(net, subnet_text, label);
        !err.empty()) {
        return {err};
    }
    if (std::string err = document.save(path); !err.empty()) {
        return {err};
    }
    reload_map_file();
    return {"", true};
}

AunEconetTransportExtension::MapEdit
AunEconetTransportExtension::remove_map_subnet(std::uint8_t net) {
    resolve_map_file_path();
    std::string path;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) return {"the map file is disabled (map-file=none)"};
        path = map_file_filepath_;
    }
    auto load = AunMapDocument::load(path);
    if (!load.document) return {load.error};
    auto document = *load.document;
    bool removed = document.remove_subnet(net);
    if (removed) {
        if (std::string err = document.save(path); !err.empty()) {
            return {err};
        }
        reload_map_file();
    }
    return {"", removed};
}

AunEconetTransportExtension::MapListing AunEconetTransportExtension::list_map() {
    resolve_map_file_path();
    std::string path;
    {
        std::lock_guard<std::mutex> lock(map_file_mutex_);
        if (!map_file_enabled_) return {};  // disabled -> empty, no error
        path = map_file_filepath_;
    }
    AunMapLoadResult loaded = load_aun_map(path);
    MapListing listing;
    if (!loaded.map.has_value()) {
        listing.error = loaded.error;
        return listing;
    }
    for (const auto& peer : loaded.map->peers) {
        auto ip = resolve_host_bounded(peer.host, std::chrono::seconds(3));
        listing.peers.push_back(ListedMapPeer{
            peer.net, peer.stn, peer.host, peer.port, peer.label, ip.has_value(),
            ip ? ip_to_dotted(*ip) : std::string{}});
    }
    for (const auto& subnet : loaded.map->subnets) {
        listing.subnets.push_back(
            ListedMapSubnet{subnet.net, subnet.subnet_text, subnet.label});
    }
    return listing;
}

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

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

#include "AunMapFile.hpp"
#include "AunPeerSet.hpp"
#include "AunUi.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
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

    // What the transport does on `_aun._udp` (issue #158, the `discovery=`
    // parameter). On = announce AND browse (the default, today's behaviour);
    // Announce = publish our record but adopt nothing; Browse = adopt
    // discovered peers but publish nothing; Off = neither. Collision reporting
    // and parked adoption need us to browse, so they are active only in On and
    // Browse; same-instant auto-station races need us to both claim and observe,
    // so they are arbitrated only in On.
    enum class DiscoveryMode { On, Announce, Browse, Off };

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

    // Choose a free station number by browsing `_aun._udp` for the numbers
    // already in use on this net (discovered announcements, map-file peers and
    // launch map= entries, but not subnet guesses), taking the lowest free one,
    // then announcing it and settling briefly so that when two instances pick
    // the same free number in the same second the #147 newcomer yields and
    // tries the next. Bounded; runs on the launch path before create_backend.
    // See issue #67 and docs/networking.md.
    AutoStationOutcome select_auto_station(econet::StationRange range) override;

    // Override the selection timings so the hermetic mDNS tests can widen the
    // observation window (real mDNS discovery is variable) without changing the
    // launch-time defaults. Call before select_auto_station.
    void set_auto_station_timings_for_test(std::chrono::milliseconds min_observe,
                                           std::chrono::milliseconds quiet,
                                           std::chrono::milliseconds budget) {
        auto_min_observe_ = min_observe;
        auto_quiet_ = quiet;
        auto_budget_ = budget;
    }

    // AUN-specific operations (peer table, cable plug, port status), served
    // through the core's ExtensionRpc channel rather than a hosted gRPC service.
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

    // Non-owning pointer to the AunBackend we constructed. Returns
    // nullptr before create_backend has run or if construction failed
    // (port=none, bind error). AunUi consults this to decide whether to show
    // the Connect button and UDP-port line; the peer table itself lives in
    // peer_set().
    AunBackend* backend() { return backend_; }
    const AunBackend* backend() const { return backend_; }

    // The desired peer world, owned here so it outlives any backend: entries
    // added before the socket is up survive until create_backend applies them,
    // and persist across backend recreation (the #55 ownership change). AunUi
    // and the dispatcher read it; the discovery subscriber writes Discovered
    // entries to it.
    AunPeerSet& peer_set() { return peer_set_; }
    const AunPeerSet& peer_set() const { return peer_set_; }

    // Add or replace an AunService.AddPeer (Api-provenance) entry. Works whether
    // or not a backend is up: with one, the resolved routing view is applied;
    // without, the entry waits in the peer set for the next create_backend.
    void add_api_peer(std::uint8_t net, std::uint8_t stn,
                      std::uint32_t ip_addr_net_byte_order, std::uint16_t port);

    // Remove the Api-provenance entry for (net, stn). If a lower-precedence
    // source (a discovered peer, say) also names it, that one becomes the
    // winner -- removal falls back rather than dropping the station.
    void remove_api_peer(std::uint8_t net, std::uint8_t stn);

    // Record the desired cable state (AunService.SetConnected). Applied to the
    // backend immediately when one exists; otherwise remembered and applied at
    // the next create_backend. Returns true if a backend was present to apply
    // it to (so the dispatcher can note a deferred request).
    bool set_desired_connected(bool connected);
    bool desired_connected() const { return desired_connected_; }

    // Override the mDNS service type the discovery announcer advertises and
    // the subscriber browses (default "_aun._udp"). Set before create_backend;
    // it is applied to the announcer and subscriber it builds. Tests give each
    // a per-test-unique type so a real AUN instance announcing "_aun._udp" on
    // this host or the LAN cannot be adopted into the peer set and fail an
    // otherwise-hermetic test. Empty keeps the default. Mirrors
    // AunDiscoveryAnnouncer/Subscriber::set_service_type, which the real-mDNS
    // [.mdns] tests already use directly.
    void set_discovery_service_type(std::string service_type) {
        discovery_service_type_ = std::move(service_type);
    }

    // --- Map file (aun-map.json) ---

    struct ReloadResult {
        bool reloaded = false;  // false only when the map file is disabled
        std::string error;      // parse error, if the file was present but bad
    };

    // Re-read the map file now, replacing the MapFile peer layer and the MapFile
    // subnet rules (Api, Launch and Discovered are untouched). Runs hostname
    // resolution, so it must be called off the emulation thread. Safe before a
    // backend exists -- the peer set absorbs it.
    ReloadResult reload_map_file();

    // The resolved map-file path on this host (empty when map-file=none), its
    // entry count from the last load, and the last load error (empty on
    // success or an absent file). For AunService.GetStatus.
    std::string map_file_path() const;
    std::uint32_t map_file_entry_count() const;
    std::string map_file_error() const;

    // Map peers whose host did not resolve on the last load: kept for display
    // as unreachable rather than routed. Copied out under the lock.
    std::vector<AunMapPeer> unreachable_map_peers() const;

    // The map file's peers and subnets (with their labels) as of the last load,
    // cached so the sidebar can show labels and the subnet-rules group without
    // re-reading or re-resolving the file on every view build.
    std::vector<AunMapPeer> map_peers() const;
    std::vector<AunMapSubnet> map_subnets() const;

    // The result of a map-file edit RPC: an empty error means success, and
    // `removed` reports whether a remove found an entry.
    struct MapEdit {
        std::string error;
        bool removed = false;
    };

    // Edit the map file (AunService.AddMapPeer / RemoveMapPeer / AddMapSubnet /
    // RemoveMapSubnet): write the server's own file atomically, then apply the
    // change to the peer set at once (a reload). Validation errors name the
    // field. Must be called off the emulation thread (writes + reload resolve
    // hostnames). Safe before a backend exists.
    MapEdit add_map_peer(std::uint8_t net, std::uint8_t stn,
                         const std::string& host, std::uint16_t port,
                         const std::string& label);
    MapEdit remove_map_peer(std::uint8_t net, std::uint8_t stn);
    MapEdit add_map_subnet(std::uint8_t net, const std::string& subnet_text,
                           const std::string& label);
    MapEdit remove_map_subnet(std::uint8_t net);

    // One map-file peer as listed (with its host-resolution state) and one
    // subnet, for AunService.ListMap.
    struct ListedMapPeer {
        std::uint8_t net;
        std::uint8_t stn;
        std::string host;
        std::uint16_t port;
        std::string label;
        bool resolved;
        std::string resolved_ip;  // dotted-quad when resolved, else empty
    };
    struct ListedMapSubnet {
        std::uint8_t net;
        std::string subnet;
        std::string label;
    };
    struct MapListing {
        std::vector<ListedMapPeer> peers;
        std::vector<ListedMapSubnet> subnets;
        std::string error;  // non-empty if the file was present but malformed
    };

    // List the map file's entries with labels and host resolution (distinct
    // from the live routing table that peer_set()/ListPeers report).
    MapListing list_map();

    // When there is no working backend, the specific reason -- captured from
    // the failed AunBackend construction, naming the port and the OS cause
    // (e.g. "could not bind UDP port 32768 (Address already in use)"). Empty
    // when a backend is present or when no bind was attempted (port=none).
    // AunUi shows this verbatim in place of a generic "unavailable" line so
    // the network sidebar names the actual fault. See create_backend.
    const std::string& unavailable_reason() const { return unavailable_reason_; }

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
    // historical default before this parameter existed). Otherwise parsed as
    // decimal in the range 0..255 -- the full Econet net byte, which a guest
    // can address. Invalid input falls back to 0 with a warning to stderr.
    static std::uint8_t parse_net(const std::string& value);

    // Parse a list of "map" entries. Each element is one peer of the
    // form "[net.]stn@ip@port". Entries that can't be parsed are
    // dropped with a warning to stderr.
    static std::vector<PeerSpec> parse_map(std::span<const std::string> entries);

    // Parse the "discovery" config value. Empty/missing -> On (the default).
    // "on"/"announce"/"browse"/"off" (case-insensitive) map to the modes; any
    // other value is nullopt, which config_error() turns into a launch error.
    static std::optional<DiscoveryMode> parse_discovery_mode(const std::string& value);

    // The discovery mode in force, parsed live from the config (so it is
    // accurate even before create_backend). An invalid value -- rejected at
    // launch by config_error() -- reports as On here.
    DiscoveryMode discovery_mode() const {
        auto value = config_value("discovery");
        return parse_discovery_mode(value ? std::string(*value) : std::string{})
            .value_or(DiscoveryMode::On);
    }

    // The mode as the wire/UI string: "on", "announce", "browse", "off".
    static std::string discovery_mode_name(DiscoveryMode mode);

    // #158: reject an invalid discovery= value at launch, naming the choices.
    std::optional<std::string> config_error() const override;

private:
    AunBackend* backend_ = nullptr;  // non-owning; lives in EconetSocket
    std::string unavailable_reason_;  // why there is no backend (bind failure)

    // The desired peer world. Declared before announcer_/subscriber_ so it is
    // destroyed AFTER them: the subscriber holds a reference to it and runs
    // callbacks on the mDNS browser thread, so it must stop before the peer set
    // goes away (the same reverse-destruction-order argument the backend ref
    // relies on). Survives backend recreation, carrying Api-provenance entries.
    AunPeerSet peer_set_;
    // Desired cable state, applied to the backend when one exists. AUN comes up
    // connected; SetConnected before the backend is up records the wish here.
    bool desired_connected_ = true;

    // Non-default mDNS service type for discovery, applied to the announcer and
    // subscriber in create_backend. Empty means the default "_aun._udp". Used
    // by tests to stay hermetic against real AUN announcers (see the setter).
    std::string discovery_service_type_;

    // Discovery mode in force (issue #158). Set from the "discovery" config in
    // create_backend / select_auto_station; On until then.
    DiscoveryMode discovery_mode_ = DiscoveryMode::On;

    // Map-file state, guarded because AunService.GetStatus reads it on a gRPC
    // thread while a reload runs on create_backend / the sweep thread.
    mutable std::mutex map_file_mutex_;
    bool map_file_enabled_ = true;            // false when map-file=none
    std::string map_file_filepath_;           // resolved path (empty if disabled)
    std::uint32_t map_file_entry_count_ = 0;
    std::string map_file_error_;
    std::vector<AunMapPeer> unreachable_map_peers_;
    std::vector<AunMapPeer> map_peers_;      // the file's peers (with labels)
    std::vector<AunMapSubnet> map_subnets_;  // the file's subnets (with labels)
    // The map file's modification time at the last load, so the sweep poll can
    // detect a change. file_time_type::min() stands for an absent file.
    std::filesystem::file_time_type map_file_mtime_ =
        std::filesystem::file_time_type::min();

    // Resolve the effective map-file path from --aun map-file= / the
    // BEEBIUM_AUN_MAP_FILEPATH env / the shared per-user default, and whether it
    // is enabled (map-file=none disables). Called once in create_backend.
    void resolve_map_file_path();

    // Poll the map file's modification time and reload on a change. Wired to the
    // subscriber's sweep, so a hand or GUI edit reaches this instance within a
    // sweep interval with no platform file-watch code. Runs off the emulation
    // thread.
    void poll_map_file();

    // Seed the operator peer layers (Launch from --aun map=/subnet=, MapFile
    // from the per-user file) into the peer set for `local_net`, clearing the
    // launch-scoped layers first. Shared by create_backend and
    // select_auto_station so both see the same occupied stations.
    void seed_operator_layers(std::uint8_t local_net);

    // Timings for select_auto_station (issue #67). It keeps a claim
    // announcement up and polls: it observes for at least auto_min_observe_ (so
    // a pre-existing peer is discovered before settling), settles once the
    // chosen number has been quiet for auto_quiet_, and never runs past
    // auto_budget_. Real mDNS discovery is variable, so these are generous; the
    // whole selection usually finishes well inside the budget. Tests widen them
    // via set_auto_station_timings_for_test.
    std::chrono::milliseconds auto_min_observe_{1500};
    std::chrono::milliseconds auto_quiet_{600};
    std::chrono::milliseconds auto_budget_{4000};

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
    // Serialises the station-change re-announce (which runs on a gRPC thread
    // via the backend's station-changed callback) against itself; setup in
    // create_backend runs single-threaded before any gRPC call.
    std::mutex discovery_mutex_;
    AunUi ui_{*this};
    // Liveness token for the backend's station-changed callback. Declared last
    // so it is destroyed first: a callback that fires during teardown sees the
    // weak_ptr expired and does nothing, so the backend's stored callback never
    // dereferences a half-destroyed extension -- and the extension needs no
    // custom destructor to detach it (see create_backend).
    std::shared_ptr<bool> callback_alive_;
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_ECONET_TRANSPORT_EXTENSION_HPP

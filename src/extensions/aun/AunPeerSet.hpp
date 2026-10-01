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

#ifndef BEEBIUM_EXTENSIONS_AUN_AUN_PEER_SET_HPP
#define BEEBIUM_EXTENSIONS_AUN_AUN_PEER_SET_HPP

// The AUN peer set: the desired routing world the AUN transport maintains,
// owned by AunEconetTransportExtension rather than by any one backend. It
// outlives backend creation and recreation, so a peer added before the socket
// is up (AunService.AddPeer against an inactive transport) survives until the
// backend comes up and is applied then -- the ownership change behind #55.
//
// Each (net, stn) may be named by more than one source at once. An entry keeps
// its PROVENANCE, and the set resolves a single winner per (net, stn) by a
// fixed precedence (highest first): Api, Launch, MapFile, Discovered. An
// explicit instruction for this process beats one for this launch, which beats
// the standing map file, which beats what the network discovered. Removing the
// winner falls back to the next-highest source still present rather than
// dropping the station outright -- so taking away an AddPeer entry reveals a
// discovered one again, which the two-value model could not express (see
// docs/discussion/aun-peer-map-file.md section 2.3).
//
// AunBackend holds only the resolved routing view for its live socket; the
// extension applies this set's resolution to it (AunPeerSet::attach, step 3 of
// this change). This header is the value type alone.

#include <beebium/econet/NetworkBackend.hpp>  // StationCollisionReport

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace beebium {

class AunBackend;

// Where a peer entry came from, in descending precedence order: the smaller
// the value, the higher it wins. Four sources replace the earlier two-value
// PeerSource; the first three are all "operator" sources (they beat
// Discovered), preserving the old "operator always wins" rule.
enum class AunPeerProvenance : std::uint8_t {
    Api = 0,         // AunService.AddPeer at runtime
    Launch = 1,      // --aun map= / subnet= on the command line, or a preset's
    MapFile = 2,     // the per-user aun-map.json
    Discovered = 3,  // an _aun._udp mDNS announcement
    Subnet = 4,      // derived from a subnets rule (inbound id or outbound guess)
};

// A resolved peer: the winning entry for a (net, stn), with the provenance it
// won from. ip_addr is network byte order and is the advertised address, NOT
// the loopback rewrite a same-host peer gets in the backend's routing view.
struct AunPeerEntry {
    std::uint8_t net = 0;
    std::uint8_t stn = 0;
    std::uint32_t ip_addr = 0;
    std::uint16_t port = 0;
    AunPeerProvenance provenance = AunPeerProvenance::Discovered;
};

class AunPeerSet {
public:
    struct Endpoint {
        std::uint32_t ip_addr = 0;  // network byte order
        std::uint16_t port = 0;     // host byte order
    };

    AunPeerSet() = default;

    AunPeerSet(const AunPeerSet&) = delete;
    AunPeerSet& operator=(const AunPeerSet&) = delete;

    // Add or replace the entry for `provenance` at (net, stn). Returns true if
    // the resolved winner for (net, stn) changed (a new winner, or the winning
    // endpoint moved) -- the caller uses this to decide whether to re-render or
    // re-announce. A lower-precedence add behind an existing higher winner
    // returns false but is still recorded, so it can surface later by fall-back.
    bool set_peer(std::uint8_t net, std::uint8_t stn, std::uint32_t ip_addr,
                  std::uint16_t port, AunPeerProvenance provenance);

    // Remove the entry for `provenance` at (net, stn), if present. If a
    // lower-precedence entry remains it becomes the winner (fall-back). Returns
    // true if the resolved winner changed.
    bool remove_peer(std::uint8_t net, std::uint8_t stn,
                     AunPeerProvenance provenance);

    // Drop every entry of one provenance across all stations. Used when a
    // backend is (re)created to re-seed the Launch and Discovered layers from
    // scratch while the Api and MapFile layers persist, and on a map-file
    // reload to replace the MapFile layer.
    void clear_provenance(AunPeerProvenance provenance);

    // Add or replace the subnet rule for `net` at `provenance` (one /24 per net
    // per source; resolved by the same precedence as peers). base_ip is the
    // network byte order /24 network address. A subnet rule drives both halves
    // of the RISC OS convention when the backend applies it.
    void set_subnet_rule(std::uint8_t net, std::uint32_t base_ip,
                         AunPeerProvenance provenance);

    // Drop every subnet rule of one provenance. Used to replace the MapFile
    // rules on reload while Launch rules persist.
    void clear_subnet_rules(AunPeerProvenance provenance);

    // True if (net, stn) has an operator entry (Api, Launch or MapFile) --
    // i.e. a Discovered add would be shadowed. Preserves the old
    // is_operator_configured predicate the discovery subscriber relies on.
    bool is_operator_configured(std::uint8_t net, std::uint8_t stn) const;

    // The resolved winning endpoint for (net, stn), or nullopt if no source
    // names it.
    std::optional<Endpoint> resolve(std::uint8_t net, std::uint8_t stn) const;

    // The endpoint recorded for a SPECIFIC provenance at (net, stn), ignoring
    // precedence, or nullopt if that source has no entry. The discovery
    // subscriber uses this to tell whether a withdrawn advertisement is still
    // the Discovered-layer entry it added.
    std::optional<Endpoint> endpoint_in(std::uint8_t net, std::uint8_t stn,
                                        AunPeerProvenance provenance) const;

    // Number of (net, stn) pairs with at least one entry (i.e. resolved peers).
    std::size_t peer_count() const;

    // The resolved winners, one AunPeerEntry per (net, stn), each carrying the
    // provenance it won from. Order is unspecified.
    std::vector<AunPeerEntry> list_peers() const;

    // This station's local network number, used by the discovery subscriber's
    // self-filter. Set at backend creation from the --aun net= config.
    void set_local_net(std::uint8_t net);
    std::uint8_t local_net() const;

    // Set the station-collision report to the collisions CURRENTLY IN EFFECT
    // (count + description of the most recent still in effect; count 0 and an
    // empty description when none). The discovery subscriber recomputes and
    // pushes this from its own state whenever the set changes. When a backend
    // is attached, forwards it so the Econet status (read via
    // NetworkBackend::station_collisions) and WatchEconetStatus see the
    // clearance too.
    void set_collision_report(std::uint32_t count, std::string last);

    // The station collisions currently in effect (count + most-recent
    // description).
    NetworkBackend::StationCollisionReport station_collisions() const;

    // Bind this set to a live backend: apply the current resolution to it now,
    // and keep applying on every later change. Passing nullptr detaches.
    // Applying is atomic on the backend (one replace, under its lock).
    void attach(AunBackend* backend);

private:
    static std::uint16_t make_key(std::uint8_t net, std::uint8_t stn) {
        return (static_cast<std::uint16_t>(net) << 8) | stn;
    }

    // Winner endpoint for `key` under mutex_ already held, or nullopt.
    std::optional<Endpoint> resolve_locked(std::uint16_t key) const;

    // Push the resolved routing view to the attached backend (if any). Called
    // with mutex_ held; reaches into the backend (which takes its own peer-table
    // lock) in the fixed order peer-set-then-backend. The backend never calls
    // back into the peer set, so holding mutex_ across the apply cannot deadlock
    // and keeps each apply a single atomic replacement of the routing view.
    void apply_to_backend_locked();

    mutable std::mutex mutex_;

    // (net<<8|stn) -> provenance -> endpoint. The inner map is ordered by
    // AunPeerProvenance, so begin() is the highest-precedence (winning) entry.
    std::map<std::uint16_t, std::map<AunPeerProvenance, Endpoint>> layers_;

    // net -> provenance -> /24 network address (network byte order). Resolved
    // by the same precedence as peers; the winning rule per net is pushed to
    // the backend, which applies both halves of the subnet convention.
    std::map<std::uint8_t, std::map<AunPeerProvenance, std::uint32_t>>
        subnet_rules_;

    std::uint8_t local_net_ = 0;

    std::uint32_t collision_count_ = 0;
    std::string last_collision_;

    AunBackend* backend_ = nullptr;  // non-owning; the live routing view
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_AUN_AUN_PEER_SET_HPP

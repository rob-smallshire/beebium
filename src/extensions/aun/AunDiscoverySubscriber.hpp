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

#ifndef BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP
#define BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP

// Subscribes to "_aun._udp" DNS-SD announcements and feeds the
// resulting peers into the AUN transport's AunPeerSet as Discovered entries.
//
// Filtering rules (see docs/discussion/aun-mdns-peer-discovery.md):
//   1. Skip our own announcement -- match by the (net, station) pair
//      from the TXT record against the peer set's local (net, stn).
//   2. Operator sources (Api, Launch, MapFile) win over Discovered; the
//      subscriber records a discovered peer whether or not an operator source
//      shadows it, so removing the operator entry later reveals it again, but
//      only notifies when the resolved winner actually changes.
//
// Lifetime is RAII-shaped, mirroring AunDiscoveryAnnouncer:
// construction captures the dependencies, start() begins browsing,
// stop() / destruction tears it down.

#include <beebium/discovery/Browser.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace beebium {

class AunPeerSet;

class AunDiscoverySubscriber {
public:
    // peers (the AUN transport's peer set) must outlive the subscriber. browser
    // is the platform browser to drive; tests inject a fake. If browser is null,
    // the constructor allocates a discovery::create_browser() default.
    // own_identity is this machine's UUID (the same value the announcer
    // publishes as the impl-identity TXT record); it lets the subscriber
    // recognise its own announcement reflected back by Bonjour even when
    // Bonjour has renamed the instance. Empty when no identity was injected,
    // in which case our-number advertisements are skipped silently (we cannot
    // tell our own reflection from another machine).
    AunDiscoverySubscriber(AunPeerSet& peers,
                           std::uint8_t local_stn,
                           std::unique_ptr<discovery::Browser> browser = nullptr,
                           std::string own_identity = {});

    ~AunDiscoverySubscriber();

    AunDiscoverySubscriber(const AunDiscoverySubscriber&) = delete;
    AunDiscoverySubscriber& operator=(const AunDiscoverySubscriber&) = delete;

    // Begin browsing for "_aun._udp". Returns true if the browser
    // accepted the request, false if mDNS is unavailable on this
    // platform (the subscriber becomes a no-op for the rest of its
    // life).
    bool start();

    // Withdraw the subscription. Safe to call before start() or
    // after stop(); also called by the destructor.
    void stop();

    // True between a successful start() and a stop() call.
    bool is_subscribed() const;

    // Install a callback fired (without holding any subscriber lock)
    // after every successful peer-table mutation that the subscriber
    // makes -- both add and remove paths. The extension wires this to
    // its ExtensionUi::mark_dirty() so the AUN panel re-renders when
    // a peer comes or goes. The callback runs on the browser's
    // background thread; consumers that touch their own state from
    // it must arrange their own synchronisation.
    void set_on_peers_changed(std::function<void()> cb);

    // Install a callback fired once at the end of every liveness sweep (the
    // ~2.5 s cadence), on the sweep thread. The extension wires this to its
    // map-file mtime poll, so a hand or GUI edit is picked up within a sweep
    // interval without any platform file-watch code. Runs off the emulation
    // thread, so the poll may resolve hostnames.
    void set_on_sweep(std::function<void()> cb);

    // Override the DNS-SD service type to browse (default "_aun._udp").
    // Production always uses the default; this exists so real-mDNS tests
    // can run on a per-test-unique type and stay isolated from any stray
    // _aun._udp records on the machine. Call before start().
    void set_service_type(std::string service_type) {
        service_type_ = std::move(service_type);
    }

    // Set this machine's bind-time "since" (unix seconds) -- the same value the
    // announcer publishes -- so an own-number collision can tell whether we
    // bound before or after the claimant. The earlier `since` is the incumbent;
    // ties break on impl-identity; a claimant with no `since` is treated as the
    // incumbent (so we are the newcomer). The transport sets it at bind time
    // and refreshes it on a station change. Atomic: set from the gRPC thread
    // while handle_added reads it on the browser thread. See #147.
    void set_own_since(std::int64_t since_unix_seconds) {
        own_since_.store(since_unix_seconds, std::memory_order_relaxed);
    }

    // Test-only: parse a TXT record set into the (net, stn) pair the
    // subscriber would derive. Returns nullopt if the schema is
    // missing or invalid (so the subscriber can't safely act on it).
    static bool parse_txt(const std::map<std::string, std::string>& txt,
                          std::uint8_t& net_out,
                          std::uint8_t& stn_out);

    // Test-only: invoke the on_added handler directly so unit tests
    // don't need a real browser. Pass a fully-populated
    // DiscoveredService.
    void inject_added(const discovery::DiscoveredService& svc);

    // Test-only counterpart to inject_added.
    void inject_removed(const std::string& instance_name);

    // Re-evaluate every known discovered peer against the current map file: a
    // discovered (net, stn) the file maps to a different endpoint is a
    // disagreement, one that now agrees or is no longer in the file clears.
    // Called by the extension after a map-file reload. Republishes the report.
    void revalidate_file_disagreements();

    // Run one same-host liveness sweep synchronously: for every same-host
    // (loopback) peer, bind-probe its port and reap the ones whose server has
    // exited. The production timer thread calls this every few seconds; tests
    // call it directly so they don't depend on the thread's cadence. Safe to
    // call whether or not start() has run.
    void sweep_once();

    // Update the station this subscriber self-filters. After the guest's
    // station changes at runtime (EconetService::SetStationId), an
    // advertisement carrying the OLD station is no longer us and must be
    // accepted as a peer, while one carrying the NEW station is now us and must
    // be skipped. Callable from the gRPC thread while handle_added runs on the
    // browser thread, so the field is atomic. Also clears any own-number
    // collisions in effect -- a claimant of our FORMER number is no longer
    // claiming ours -- and republishes the report.
    void set_local_station(std::uint8_t local_stn);

private:
    AunPeerSet& peers_;
    std::atomic<std::uint8_t> local_stn_;
    std::unique_ptr<discovery::Browser> browser_;
    std::string own_identity_;  // our impl-identity (machine UUID), for self-recognition
    // This machine's bind-time "since" (unix seconds), mirroring the announcer's,
    // for incumbent-vs-newcomer resolution on an own-number collision (#147).
    std::atomic<std::int64_t> own_since_;
    std::string service_type_ = "_aun._udp";
    bool trace_ = false;  // BEEBIUM_AUN_TRACE: log collisions to stderr

    // Interval between same-host liveness sweeps on the production thread.
    static constexpr std::chrono::milliseconds kSweepInterval{2500};

    // What a discovered peer maps to. same_host peers (advertised on one of
    // this host's own IPs, so add_peer rerouted them to 127.0.0.1) have their
    // lifetime governed by the bind-probe sweep, NOT by mDNS removal -- a NIC
    // change withdraws the advertisement but loopback stays reachable. port is
    // the loopback UDP port (host byte order) the sweep probes.
    struct PeerRef {
        std::uint8_t net;
        std::uint8_t stn;
        bool same_host;
        std::uint16_t port;
        std::uint32_t ip;  // advertised address (network byte order), for
                           // re-checking a map-file disagreement on reload
    };

    // Maps DNS-SD instance name -> the peer it was registered as, so
    // on_removed / the sweep can find which peer to drop. The browser only
    // delivers the instance name on remove, not the TXT records.
    mutable std::mutex name_map_mutex_;
    std::map<std::string, PeerRef> name_to_peer_;

    // A discovered advertisement refused as a collision (its (net, stn) was
    // held by a different live station), kept so it can be adopted once that
    // number frees. seq orders them: on adoption the most recent for a given
    // (net, stn) wins. Dropped when the advertisement's own name is withdrawn.
    // Each parked entry is also a peer-vs-peer collision currently in effect;
    // description is the line the status report shows. seq also orders the live
    // collision set (pending_ and own_collisions_ share pending_seq_), so the
    // "most recent in effect" is the entry with the highest seq.
    struct PendingRef {
        std::uint8_t net;
        std::uint8_t stn;
        std::uint32_t ip;        // network byte order, as advertised
        std::uint16_t port;
        bool same_host;
        std::uint64_t seq;
        std::string description;
    };
    std::map<std::string, PendingRef> pending_;  // guarded by name_map_mutex_
    std::uint64_t pending_seq_ = 0;               // guarded by name_map_mutex_

    // Own-number collisions currently in effect: a DIFFERENT instance (by its
    // impl-identity) advertising THIS machine's (net, stn). Keyed by the
    // claimant's DNS-SD instance name, so one clears when that name is withdrawn
    // (handle_removed) or when this machine's own station changes
    // (set_local_station). Unlike a parked peer there is nothing to adopt -- we
    // never yield our own number. Guarded by name_map_mutex_.
    struct OwnNumberCollision {
        std::uint64_t seq;
        std::string description;
    };
    std::map<std::string, OwnNumberCollision> own_collisions_;  // name_map_mutex_

    // File-vs-discovered disagreements currently in effect (#139): a discovered
    // (net, stn) the map file maps to a DIFFERENT endpoint. The map file wins
    // the routing (MapFile outranks Discovered); the disagreement is surfaced
    // through the same live collision set, keyed by the discovered instance
    // name, and clears when that announcement withdraws or the file changes.
    std::map<std::string, OwnNumberCollision> file_disagreements_;  // name_map_mutex_

    // Same-host liveness sweep thread (started by start(), joined by stop()).
    std::thread sweep_thread_;
    std::mutex sweep_mutex_;
    std::condition_variable sweep_cv_;
    bool sweep_stop_ = false;

    // Snapshot read out under name_map_mutex_; invoked outside the
    // lock so re-entrant callers can call back into the subscriber.
    mutable std::mutex callback_mutex_;
    std::function<void()> on_peers_changed_;
    std::function<void()> on_sweep_;

    void handle_added(const discovery::DiscoveredService& svc);
    void handle_removed(const std::string& instance_name);
    void notify_peers_changed();
    void sweep_loop();  // body of sweep_thread_

    // A station just freed up (its holder was removed or reaped): adopt the
    // most recent pending advertisement for that (net, stn), if any, through
    // the normal add path.
    void adopt_pending(std::uint8_t net, std::uint8_t stn);

    // Record or clear the map-file disagreement for one discovered peer against
    // the current map file. Returns true if the live collision set changed.
    // Takes name_map_mutex_ internally; call without it held.
    bool note_file_disagreement(const std::string& instance_name,
                                std::uint8_t net, std::uint8_t stn,
                                std::uint32_t ip, std::uint16_t port);

    // Recompute the station-collision report (pending_ + own_collisions_) and
    // push it to the peer set, which forwards it to the backend. Call after any
    // change to the live collision set. Snapshots under name_map_mutex_ and
    // publishes after releasing it.
    void publish_collision_report();
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP

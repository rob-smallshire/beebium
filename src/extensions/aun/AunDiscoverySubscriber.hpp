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
// resulting peers into AunBackend's peer table as Discovered entries.
//
// Filtering rules (see docs/discussion/aun-mdns-peer-discovery.md):
//   1. Skip our own announcement -- match by the (net, station) pair
//      from the TXT record against the backend's local (net, stn).
//   2. Operator-configured entries always win; the subscriber relies
//      on AunBackend::add_peer's PeerSource precedence (a Discovered
//      add against an existing operator entry is dropped silently).
//
// Lifetime is RAII-shaped, mirroring AunDiscoveryAnnouncer:
// construction captures the dependencies, start() begins browsing,
// stop() / destruction tears it down.

#include <beebium/discovery/Browser.hpp>

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

class AunBackend;

class AunDiscoverySubscriber {
public:
    // backend must outlive the subscriber. browser is the platform
    // browser to drive; tests inject a fake. If browser is null, the
    // constructor allocates a discovery::create_browser() default.
    AunDiscoverySubscriber(AunBackend& backend,
                           std::uint8_t local_stn,
                           std::unique_ptr<discovery::Browser> browser = nullptr);

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

    // Override the DNS-SD service type to browse (default "_aun._udp").
    // Production always uses the default; this exists so real-mDNS tests
    // can run on a per-test-unique type and stay isolated from any stray
    // _aun._udp records on the machine. Call before start().
    void set_service_type(std::string service_type) {
        service_type_ = std::move(service_type);
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

    // Run one same-host liveness sweep synchronously: for every same-host
    // (loopback) peer, bind-probe its port and reap the ones whose server has
    // exited. The production timer thread calls this every few seconds; tests
    // call it directly so they don't depend on the thread's cadence. Safe to
    // call whether or not start() has run.
    void sweep_once();

private:
    AunBackend& backend_;
    std::uint8_t local_stn_;
    std::unique_ptr<discovery::Browser> browser_;
    std::string service_type_ = "_aun._udp";

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
    };

    // Maps DNS-SD instance name -> the peer it was registered as, so
    // on_removed / the sweep can find which peer to drop. The browser only
    // delivers the instance name on remove, not the TXT records.
    mutable std::mutex name_map_mutex_;
    std::map<std::string, PeerRef> name_to_peer_;

    // Same-host liveness sweep thread (started by start(), joined by stop()).
    std::thread sweep_thread_;
    std::mutex sweep_mutex_;
    std::condition_variable sweep_cv_;
    bool sweep_stop_ = false;

    // Snapshot read out under name_map_mutex_; invoked outside the
    // lock so re-entrant callers can call back into the subscriber.
    mutable std::mutex callback_mutex_;
    std::function<void()> on_peers_changed_;

    void handle_added(const discovery::DiscoveredService& svc);
    void handle_removed(const std::string& instance_name);
    void notify_peers_changed();
    void sweep_loop();  // body of sweep_thread_
};

}  // namespace beebium

#endif  // BEEBIUM_ECONET_AUN_DISCOVERY_SUBSCRIBER_HPP

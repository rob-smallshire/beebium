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

#include "AunDiscoverySubscriber.hpp"

#include <beebium/econet/AunBackend.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace beebium {

namespace {

// Format a network-byte-order IPv4 + host-order port as "a.b.c.d:port", using
// only the raw bytes so this needs no socket headers.
std::string format_endpoint(std::uint32_t ip_net_order, std::uint16_t port) {
    unsigned char b[4];
    std::memcpy(b, &ip_net_order, sizeof(b));
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." +
           std::to_string(b[2]) + "." + std::to_string(b[3]) + ":" +
           std::to_string(port);
}

bool parse_byte(const std::string& s, std::uint8_t& out) {
    if (s.empty()) return false;
    unsigned long v = 0;
    auto first = s.data();
    auto last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc{} || ptr != last || v > 255) return false;
    out = static_cast<std::uint8_t>(v);
    return true;
}

}  // namespace

AunDiscoverySubscriber::AunDiscoverySubscriber(
        AunBackend& backend,
        std::uint8_t local_stn,
        std::unique_ptr<discovery::Browser> browser)
    : backend_(backend)
    , local_stn_(local_stn)
    , browser_(browser ? std::move(browser) : discovery::create_browser())
    , trace_(std::getenv("BEEBIUM_AUN_TRACE") != nullptr) {}

AunDiscoverySubscriber::~AunDiscoverySubscriber() {
    stop();
}

bool AunDiscoverySubscriber::start() {
    if (!browser_) return false;
    discovery::BrowserCallbacks cbs;
    cbs.on_added = [this](const discovery::DiscoveredService& svc) {
        handle_added(svc);
    };
    cbs.on_removed = [this](const std::string& name) {
        handle_removed(name);
    };
    if (!browser_->start(service_type_, std::move(cbs))) return false;

    // Start the same-host liveness sweep thread. Same-host peers are kept
    // across mDNS withdrawals (NIC changes) and reaped only when their server
    // actually exits -- the sweep is what detects that.
    {
        std::lock_guard lock(sweep_mutex_);
        sweep_stop_ = false;
    }
    if (!sweep_thread_.joinable()) {
        sweep_thread_ = std::thread([this] { sweep_loop(); });
    }
    return true;
}

void AunDiscoverySubscriber::stop() {
    if (!browser_) return;
    browser_->stop();

    {
        std::lock_guard lock(sweep_mutex_);
        sweep_stop_ = true;
    }
    sweep_cv_.notify_all();
    if (sweep_thread_.joinable()) sweep_thread_.join();

    std::lock_guard lock(name_map_mutex_);
    name_to_peer_.clear();
}

bool AunDiscoverySubscriber::is_subscribed() const {
    if (!browser_) return false;
    return browser_->state().browsing;
}

void AunDiscoverySubscriber::set_on_peers_changed(std::function<void()> cb) {
    std::lock_guard lock(callback_mutex_);
    on_peers_changed_ = std::move(cb);
}

bool AunDiscoverySubscriber::parse_txt(
        const std::map<std::string, std::string>& txt,
        std::uint8_t& net_out,
        std::uint8_t& stn_out) {
    auto v_it = txt.find("version");
    if (v_it == txt.end() || v_it->second != "1") return false;
    auto net_it = txt.find("net");
    auto stn_it = txt.find("station");
    if (net_it == txt.end() || stn_it == txt.end()) return false;
    if (!parse_byte(net_it->second, net_out)) return false;
    if (!parse_byte(stn_it->second, stn_out)) return false;
    return true;
}

void AunDiscoverySubscriber::inject_added(
        const discovery::DiscoveredService& svc) {
    handle_added(svc);
}

void AunDiscoverySubscriber::inject_removed(const std::string& instance_name) {
    handle_removed(instance_name);
}

void AunDiscoverySubscriber::handle_added(
        const discovery::DiscoveredService& svc) {
    std::uint8_t net = 0;
    std::uint8_t stn = 0;
    if (!parse_txt(svc.txt_records, net, stn)) {
        return;  // Schema invalid -- safer to ignore than to guess.
    }

    // Skip our own announcement: same (net, stn) means it's us.
    // (We compare net to backend_.local_net() so a Beebium that
    // changes its local_net at runtime correctly stops self-filtering
    // its old announcement -- though that's not currently possible.)
    if (net == backend_.local_net() &&
        stn == local_stn_.load(std::memory_order_relaxed)) {
        return;
    }

    if (svc.ipv4_addr_net_byte_order == 0 || svc.port == 0) {
        return;  // No usable endpoint yet (e.g. IPv6-only peer).
    }

    // Skip the change-callback if the operator already pinned this
    // (net, stn) -- add_peer will refuse to overwrite, so the peer
    // table didn't actually change.
    bool operator_pinned =
        backend_.is_operator_configured(net, stn);

    // Same-host? The peer advertised one of THIS host's own IPs, so add_peer
    // will reroute it to loopback. Such a peer's lifetime is governed by the
    // liveness sweep, not by mDNS removal (a NIC change withdraws its
    // advertisement while loopback stays reachable).
    bool same_host = false;
    for (std::uint32_t addr : AunBackend::local_host_ipv4_addresses()) {
        if (addr == svc.ipv4_addr_net_byte_order) {
            same_host = true;
            break;
        }
    }

    // First live station wins: if this (net, stn) is already held by a
    // DIFFERENT discovered instance, do NOT displace the incumbent -- that is
    // the "orphaned station" half of #68. A re-advertisement from the SAME
    // instance (a NIC change, or an ephemeral-port change) is not a collision
    // and updates in place below. name_to_peer_ holds only discovered owners,
    // so this scan naturally ignores operator-configured entries (which
    // add_peer already protects).
    bool collision = false;
    {
        std::lock_guard lock(name_map_mutex_);
        for (const auto& [name, ref] : name_to_peer_) {
            if (ref.net == net && ref.stn == stn &&
                name != svc.instance_name) {
                collision = true;
                break;
            }
        }
        if (collision) {
            // Keep the refused advertisement so it can be adopted once the
            // number frees (the incumbent leaving, or its port being reaped).
            pending_[svc.instance_name] =
                PendingRef{net, stn, svc.ipv4_addr_net_byte_order, svc.port,
                           same_host, ++pending_seq_};
        }
    }
    if (collision) {
        std::string held = "?";
        if (auto ep = backend_.peer_endpoint(net, stn)) {
            held = format_endpoint(ep->first, ep->second);
        }
        std::string description =
            "station " + std::to_string(static_cast<unsigned>(net)) + "." +
            std::to_string(static_cast<unsigned>(stn)) + " already held by " +
            held + "; rejected advertisement from " +
            format_endpoint(svc.ipv4_addr_net_byte_order, svc.port);
        if (trace_) {
            std::cerr << "AUN RX: " << description << "\n";
        }
        backend_.note_station_collision(std::move(description));
        return;  // incumbent kept; newcomer parked as pending
    }

    backend_.add_peer(net, stn, svc.ipv4_addr_net_byte_order, svc.port,
                      PeerSource::Discovered);

    {
        std::lock_guard lock(name_map_mutex_);
        // Reconciles in place on a re-add (same instance name -> same key),
        // so a Wi-Fi-return re-advertisement updates the entry rather than
        // creating a duplicate. If this name had been parked as pending, it is
        // now an owner, so drop the pending record.
        name_to_peer_[svc.instance_name] = PeerRef{net, stn, same_host, svc.port};
        pending_.erase(svc.instance_name);
    }

    if (!operator_pinned) {
        notify_peers_changed();
    }
}

void AunDiscoverySubscriber::handle_removed(const std::string& instance_name) {
    PeerRef ref{};
    {
        std::lock_guard lock(name_map_mutex_);
        // A parked (refused) advertisement being withdrawn owns nothing: drop
        // it so it is never adopted later.
        if (pending_.erase(instance_name) > 0) return;

        auto it = name_to_peer_.find(instance_name);
        if (it == name_to_peer_.end()) return;
        ref = it->second;
        // SAME-HOST peer: KEEP it. mDNS withdrew the advertisement (typically
        // a Wi-Fi/Ethernet toggle), but the peer is still reachable over
        // loopback. Its removal is the liveness sweep's job, which reaps it
        // only when its server has actually exited. Leave the name mapping so
        // the sweep can still find it.
        if (ref.same_host) return;
        name_to_peer_.erase(it);
    }

    // Don't yank an operator-configured entry just because the
    // discovered shadow went away -- that would surprise an operator
    // who set the peer manually after the discovery added it.
    if (backend_.is_operator_configured(ref.net, ref.stn)) return;

    // Only drop the backend entry if it still belongs to THIS instance's
    // endpoint. If a different, still-live station now holds this (net, stn),
    // removing on the collider's withdrawal would orphan the incumbent. The
    // port discriminates instances (the loopback rewrite preserves it).
    auto endpoint = backend_.peer_endpoint(ref.net, ref.stn);
    if (!endpoint || endpoint->second != ref.port) return;

    backend_.remove_peer(ref.net, ref.stn);
    notify_peers_changed();

    // The number just freed: adopt the most recent advertisement we parked for
    // it, if any (e.g. a relaunched station that had been refused as a
    // collision while the old registration lingered).
    adopt_pending(ref.net, ref.stn);
}

void AunDiscoverySubscriber::sweep_once() {
    // Snapshot the same-host peers under the lock; probe (a blocking syscall)
    // and mutate the backend OUTSIDE it.
    struct Candidate {
        std::string name;
        std::uint8_t net;
        std::uint8_t stn;
        std::uint16_t port;
    };
    std::vector<Candidate> candidates;
    {
        std::lock_guard lock(name_map_mutex_);
        for (const auto& [name, ref] : name_to_peer_) {
            if (ref.same_host) {
                candidates.push_back({name, ref.net, ref.stn, ref.port});
            }
        }
    }

    bool changed = false;
    std::vector<std::pair<std::uint8_t, std::uint8_t>> reaped;
    for (const auto& c : candidates) {
        if (AunBackend::is_udp_port_in_use(c.port)) {
            // Still held -> peer alive -> keep (survives a NIC toggle).
            // EDGE: if the peer quit and an UNRELATED process then grabbed its
            // ephemeral port, the probe still reads "in use" and we keep a
            // phantom entry. Harmless: AUN unicast to a non-AUN listener is
            // ignored, and the phantom is corrected when the real peer
            // re-advertises (add_peer updates the entry in place). Not worth
            // over-engineering (e.g. a protocol ping) to close.
            continue;
        }
        // Port is free -> the same-host peer's server has exited. Reap it.
        // (Take peer_table_ via remove_peer BEFORE re-taking name_map_, matching
        //  handle_added's lock order; the two locks are never held together.)
        if (!backend_.is_operator_configured(c.net, c.stn)) {
            backend_.remove_peer(c.net, c.stn);
            changed = true;
        }
        {
            std::lock_guard lock(name_map_mutex_);
            name_to_peer_.erase(c.name);
        }
        reaped.push_back({c.net, c.stn});
    }

    if (changed) notify_peers_changed();

    // Each reaped station's number is now free: adopt a parked advertisement
    // for it if one is waiting (e.g. a relaunched same-host station that was
    // refused while the crashed one's port lingered).
    for (const auto& [net, stn] : reaped) {
        adopt_pending(net, stn);
    }
}

void AunDiscoverySubscriber::adopt_pending(std::uint8_t net, std::uint8_t stn) {
    std::string name;
    PendingRef ref{};
    bool found = false;
    {
        std::lock_guard lock(name_map_mutex_);
        for (const auto& [candidate_name, candidate] : pending_) {
            if (candidate.net == net && candidate.stn == stn &&
                (!found || candidate.seq > ref.seq)) {
                name = candidate_name;
                ref = candidate;
                found = true;
            }
        }
        if (found) pending_.erase(name);
    }
    if (!found) return;

    backend_.add_peer(net, stn, ref.ip, ref.port, PeerSource::Discovered);
    {
        std::lock_guard lock(name_map_mutex_);
        name_to_peer_[name] = PeerRef{net, stn, ref.same_host, ref.port};
    }
    if (trace_) {
        std::cerr << "AUN RX: adopted pending station "
                  << static_cast<unsigned>(net) << "."
                  << static_cast<unsigned>(stn) << " from "
                  << format_endpoint(ref.ip, ref.port) << "\n";
    }
    notify_peers_changed();
}

void AunDiscoverySubscriber::sweep_loop() {
    std::unique_lock lock(sweep_mutex_);
    while (!sweep_stop_) {
        // Wait the interval; wake early on stop. wait_for returns true only if
        // the predicate (stop requested) holds -> exit; false on timeout -> sweep.
        if (sweep_cv_.wait_for(lock, kSweepInterval,
                               [this] { return sweep_stop_; })) {
            break;
        }
        lock.unlock();
        sweep_once();
        lock.lock();
    }
}

void AunDiscoverySubscriber::notify_peers_changed() {
    std::function<void()> cb;
    {
        std::lock_guard lock(callback_mutex_);
        cb = on_peers_changed_;
    }
    if (cb) cb();
}

}  // namespace beebium

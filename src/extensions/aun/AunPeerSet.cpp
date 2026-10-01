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

#include "AunPeerSet.hpp"

#include <beebium/econet/AunBackend.hpp>

#include <utility>

namespace beebium {

std::optional<AunPeerSet::Endpoint>
AunPeerSet::resolve_locked(std::uint16_t key) const {
    auto it = layers_.find(key);
    if (it == layers_.end() || it->second.empty()) {
        return std::nullopt;
    }
    // The inner map is ordered by AunPeerProvenance ascending, and the enum is
    // ordered highest-precedence-first, so begin() is the winner.
    return it->second.begin()->second;
}

bool AunPeerSet::set_peer(std::uint8_t net, std::uint8_t stn,
                          std::uint32_t ip_addr, std::uint16_t port,
                          AunPeerProvenance provenance) {
    const std::uint16_t key = make_key(net, stn);
    std::lock_guard<std::mutex> lock(mutex_);

    auto before = resolve_locked(key);
    layers_[key][provenance] = Endpoint{ip_addr, port};
    auto after = resolve_locked(key);

    bool changed = !before.has_value() ||
                   before->ip_addr != after->ip_addr ||
                   before->port != after->port;
    if (changed) {
        apply_to_backend_locked();
    }
    return changed;
}

bool AunPeerSet::remove_peer(std::uint8_t net, std::uint8_t stn,
                             AunPeerProvenance provenance) {
    const std::uint16_t key = make_key(net, stn);
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = layers_.find(key);
    if (it == layers_.end()) {
        return false;
    }
    auto before = resolve_locked(key);
    if (it->second.erase(provenance) == 0) {
        return false;  // that source had no entry; nothing moved
    }
    if (it->second.empty()) {
        layers_.erase(it);
    }
    auto after = resolve_locked(key);

    bool changed = after.has_value() != before.has_value() ||
                   (after.has_value() &&
                    (after->ip_addr != before->ip_addr ||
                     after->port != before->port));
    if (changed) {
        apply_to_backend_locked();
    }
    return changed;
}

void AunPeerSet::clear_provenance(AunPeerProvenance provenance) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool changed = false;
    for (auto it = layers_.begin(); it != layers_.end();) {
        auto winner_before = it->second.begin()->first;
        if (it->second.erase(provenance) > 0) {
            if (it->second.empty()) {
                it = layers_.erase(it);
                changed = true;
                continue;
            }
            // Removing a layer only moves the winner if the removed one was it.
            if (winner_before == provenance) {
                changed = true;
            }
        }
        ++it;
    }
    if (changed) {
        apply_to_backend_locked();
    }
}

bool AunPeerSet::is_operator_configured(std::uint8_t net,
                                        std::uint8_t stn) const {
    const std::uint16_t key = make_key(net, stn);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = layers_.find(key);
    if (it == layers_.end()) {
        return false;
    }
    for (const auto& [provenance, endpoint] : it->second) {
        if (provenance != AunPeerProvenance::Discovered) {
            return true;
        }
    }
    return false;
}

std::optional<AunPeerSet::Endpoint>
AunPeerSet::resolve(std::uint8_t net, std::uint8_t stn) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolve_locked(make_key(net, stn));
}

std::optional<AunPeerSet::Endpoint>
AunPeerSet::endpoint_in(std::uint8_t net, std::uint8_t stn,
                        AunPeerProvenance provenance) const {
    const std::uint16_t key = make_key(net, stn);
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = layers_.find(key);
    if (it == layers_.end()) {
        return std::nullopt;
    }
    auto layer = it->second.find(provenance);
    if (layer == it->second.end()) {
        return std::nullopt;
    }
    return layer->second;
}

std::size_t AunPeerSet::peer_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return layers_.size();
}

std::vector<AunPeerEntry> AunPeerSet::list_peers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AunPeerEntry> result;
    result.reserve(layers_.size());
    for (const auto& [key, inner] : layers_) {
        if (inner.empty()) continue;
        const auto& [provenance, endpoint] = *inner.begin();
        result.push_back(AunPeerEntry{
            static_cast<std::uint8_t>(key >> 8),
            static_cast<std::uint8_t>(key & 0xFF),
            endpoint.ip_addr,
            endpoint.port,
            provenance,
        });
    }
    return result;
}

void AunPeerSet::set_local_net(std::uint8_t net) {
    std::lock_guard<std::mutex> lock(mutex_);
    local_net_ = net;
}

std::uint8_t AunPeerSet::local_net() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return local_net_;
}

void AunPeerSet::set_collision_report(std::uint32_t count, std::string last) {
    std::lock_guard<std::mutex> lock(mutex_);
    collision_count_ = count;
    last_collision_ = std::move(last);
    // Forward to the live backend (its own lock) so the Econet status and
    // WatchEconetStatus see the current set, including a drop back to zero.
    if (backend_ != nullptr) {
        backend_->set_station_collision_report(collision_count_, last_collision_);
    }
}

NetworkBackend::StationCollisionReport AunPeerSet::station_collisions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return NetworkBackend::StationCollisionReport{collision_count_,
                                                  last_collision_};
}

void AunPeerSet::attach(AunBackend* backend) {
    std::lock_guard<std::mutex> lock(mutex_);
    backend_ = backend;
    apply_to_backend_locked();
    if (backend_ != nullptr) {
        // A freshly (re)attached backend starts with no report; restore the
        // collisions currently in effect so its status is not stale-empty.
        backend_->set_station_collision_report(collision_count_, last_collision_);
    }
}

void AunPeerSet::apply_to_backend_locked() {
    if (backend_ == nullptr) {
        return;
    }
    std::vector<AunBackend::PeerRoute> routes;
    routes.reserve(layers_.size());
    for (const auto& [key, inner] : layers_) {
        if (inner.empty()) continue;
        const auto& endpoint = inner.begin()->second;
        routes.push_back(AunBackend::PeerRoute{
            static_cast<std::uint8_t>(key >> 8),
            static_cast<std::uint8_t>(key & 0xFF),
            endpoint.ip_addr,
            endpoint.port,
        });
    }
    backend_->replace_peers(routes);
}

}  // namespace beebium

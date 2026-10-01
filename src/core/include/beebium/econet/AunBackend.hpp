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

#pragma once

#include "AunPacket.hpp"
#include "NetworkBackend.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace beebium {

// Information about a peer in the AUN routing table. Provenance lives in the
// AUN transport's AunPeerSet, not here -- the backend holds only the resolved
// routing view.
struct PeerInfo {
    uint8_t net;
    uint8_t stn;
    uint32_t ip_addr;   // Network byte order
    uint16_t port;      // Host byte order
};

// UDP transport backend implementing the AUN (Acorn Universal Networking) protocol.
//
// Sends and receives AUN-formatted UDP datagrams. Its routing view is set by
// the AUN transport extension's AunPeerSet via replace_peers() (and add_peer()
// for direct callers and tests); peer discovery and source precedence live in
// that peer set, above the backend.
//
// The constructor creates and binds a UDP socket to the specified local port.
// If socket creation or binding fails, the backend enters a disconnected state
// (is_connected() returns false, DCD goes high, NFS ROM reports "No clock").
//
// send_frame() and receive_frame() perform direct non-blocking socket I/O.
// receive_frame() uses select() with zero timeout to avoid blocking the
// emulation thread.
class AunBackend : public NetworkBackend {
public:
    // Create a UDP socket bound to the specified local port.
    // local_net/local_stn identify this station for populating received frame addressing.
    explicit AunBackend(uint8_t local_net, uint8_t local_stn,
                        uint16_t local_port = AUN_DEFAULT_PORT);

    ~AunBackend() override;

    // Non-copyable (owns socket fd)
    AunBackend(const AunBackend&) = delete;
    AunBackend& operator=(const AunBackend&) = delete;

    // --- NetworkBackend interface ---

    void send_frame(const NetworkFrame& frame) override;
    std::optional<NetworkFrame> receive_frame() override;
    bool is_connected() const override;

    // A destination is reachable when the peer table can resolve it. Applies
    // the same dest_net=0 -> local_net translation send_frame does, so the
    // answer matches what a send would actually do.
    bool is_reachable(uint8_t net, uint8_t stn) const override;

    // The guest's station number changed at runtime (EconetService::SetStationId
    // on a gRPC thread). Updates local_stn_ (which labels dest_stn on received
    // frames) and invokes the station-changed callback so the AUN transport can
    // re-announce and update its discovery self-filter. Takes effect for
    // received-frame addressing immediately; the guest re-reads the new station
    // from &FE18 on the next Break, per the EconetSocket contract.
    void on_station_id_changed(uint8_t new_station_id) override;

    // Register a callback invoked (outside the backend's locks) whenever the
    // station changes via on_station_id_changed. The AUN transport extension
    // registers this to re-announce and update the subscriber's self-filter;
    // it clears the callback (passes nullptr) before the backend is destroyed.
    void set_station_changed_callback(std::function<void(uint8_t)> callback);

    // Register a callback invoked once from the destructor, before this
    // backend's members are torn down. The AUN transport extension registers it
    // so that when EconetSocket frees the backend on DisableEconet -- nothing
    // else tells the transport -- it can drop its dangling raw pointer to this
    // backend (detach its peer set, stop discovery) before any async writer
    // dereferences freed storage. The callback runs on whatever thread drops
    // the last reference (the gRPC thread with emulation parked for a prompt
    // disable, or a gRPC reader that co-owned the backend a moment longer).
    void set_destroyed_callback(std::function<void()> callback);

    // --- Peer management ---
    //
    // The peer table is the only AunBackend state that has multiple
    // writers. The emulator thread reads it from send_frame /
    // receive_frame; AunService and AunDiscoverySubscriber write to
    // it from gRPC and discovery threads respectively. All
    // peer-table accessors take peer_table_mutex_ briefly. The mutex
    // is uncontended in steady-state (peer changes are rare); the
    // tax buys us correctness for the multi-writer case.

    // Add or replace a single peer mapping: Econet address (net, stn) <-> UDP
    // endpoint (ip_addr network byte order, port host byte order). A same-host
    // endpoint is rewritten to loopback. Re-adding (net, stn) updates its
    // endpoint (last writer wins). Direct callers and tests use this; the AUN
    // transport applies its resolved set through replace_peers instead.
    void add_peer(uint8_t net, uint8_t stn, uint32_t ip_addr, uint16_t port);

    // Remove a peer mapping by Econet address.
    void remove_peer(uint8_t net, uint8_t stn);

    // A resolved route to install: an Econet (net, stn) mapped to a UDP
    // endpoint (ip_addr network byte order, port host byte order).
    struct PeerRoute {
        uint8_t net;
        uint8_t stn;
        uint32_t ip_addr;
        uint16_t port;
    };

    // Replace the entire routing view with `routes`, atomically under the peer
    // table lock, so a concurrent send_frame never observes a half-built table.
    // Each same-host endpoint is rewritten to loopback exactly as add_peer
    // does. This is how the AUN transport's peer set applies its resolved
    // peers; the backend itself no longer ranks sources.
    void replace_peers(std::span<const PeerRoute> routes);

    // Number of peers in the routing view.
    size_t peer_count() const;

    // The local UDP port this backend is bound to.
    uint16_t local_port() const override;

    // Why the socket failed to come up, if it did. Empty when connected.
    // Names the specific cause (port + OS reason, e.g. "could not bind UDP
    // port 32768 (Address already in use)") so a caller can surface it
    // instead of a generic "unavailable". Set once at construction.
    const std::string& bind_error() const { return bind_error_; }

    // The local network number for this station.
    uint8_t local_net() const;

    // The local station number (updated by on_station_id_changed).
    uint8_t local_station() const {
        return local_stn_.load(std::memory_order_relaxed);
    }

    // Enumerate all configured peers.
    std::vector<PeerInfo> list_peers() const;

    // The current UDP endpoint (network-order ip, host-order port) mapped to
    // (net, stn), or nullopt if that station is not in the peer table. Used by
    // the discovery subscriber to describe a collision (which endpoint holds
    // the station a newcomer tried to claim).
    std::optional<std::pair<uint32_t, uint16_t>>
    peer_endpoint(uint8_t net, uint8_t stn) const;

    // Record a rejected station-number collision (a discovered peer advertised
    // a station already held by a different, still-live endpoint). Increments
    // the count, stores the description as the most recent, and bumps the
    // status sequence so WatchEconetStatus re-reads. Called by the subscriber
    // on the browser thread.
    void note_station_collision(std::string description);

    // Collisions observed so far (count + most-recent description). Overrides
    // NetworkBackend so EconetService can surface it on the Econet status.
    StationCollisionReport station_collisions() const override;

    // Every IPv4 address of the local host (network byte order), across all
    // interfaces (Wi-Fi, Ethernet, bridges, loopback). add_peer uses this to
    // recognise a same-host peer and route it over loopback; also useful for
    // diagnostics. Re-queried live, so it reflects the current interface set.
    static std::vector<uint32_t> local_host_ipv4_addresses();

    // Liveness probe: is UDP `port` (host byte order) currently held by any
    // socket on this host? Opens a transient UDP socket and plain-binds
    // INADDR_ANY:port with NO SO_REUSEADDR/SO_REUSEPORT, reporting whether that
    // bind conflicts:
    //   true  = EADDRINUSE -> the port is held (a peer is alive)
    //   false = bind succeeded -> the port is free (the peer has quit)
    // It binds the WILDCARD (not 127.0.0.1) deliberately: the real AUN socket
    // is wildcard-bound (INADDR_ANY), so a wildcard probe collides with it on
    // macOS, Linux AND Windows. A specific-address probe (127.0.0.1) would
    // wrongly SUCCEED against a live wildcard socket on Windows (specific and
    // wildcard binds coexist there), falsely reporting the port free. The plain
    // probe (no reuse flags) is what forces the conflict even against an
    // SO_REUSEADDR owner. The probe socket is closed immediately and never
    // touches the real AUN socket. Any unexpected error is treated as "in use"
    // (conservative: don't reap on an ambiguous result). Used by the same-host
    // liveness sweep to reap a peer whose server exited versus one whose mDNS
    // advertisement merely lapsed on a NIC change.
    static bool is_udp_port_in_use(uint16_t port);

    // How many frames have been dropped because the socket's send buffer was
    // full. Non-zero means a congested link is silently losing guest traffic.
    uint64_t send_would_block_count() const { return send_would_block_count_; }

    // Simulate plugging/unplugging the network cable.
    // When disconnected, the ADLC sees DCD high (no carrier) and CTS high
    // (not clear to send). Takes effect on the next ADLC tick.
    void set_connected(bool connected);

private:
#ifdef _WIN32
    using socket_type = SOCKET;
    static constexpr socket_type invalid_socket = INVALID_SOCKET;
#else
    using socket_type = int;
    static constexpr socket_type invalid_socket = -1;
#endif

    socket_type socket_fd_ = invalid_socket;
    std::string bind_error_;  // non-empty iff the socket failed to come up
    uint16_t local_port_;
    uint8_t local_net_;
    std::atomic<uint8_t> local_stn_;  // labels dest_stn on received frames
    std::atomic<bool> connected_ = false;

    // Invoked (outside the locks) by on_station_id_changed. Registered by the
    // AUN transport extension to re-announce and update the discovery
    // self-filter; guarded because SetStationId runs on a gRPC thread.
    std::mutex station_callback_mutex_;
    std::function<void(uint8_t)> station_changed_callback_;

    // Invoked once from the destructor (see set_destroyed_callback). Guarded
    // because it is registered from a gRPC thread.
    std::mutex destroyed_callback_mutex_;
    std::function<void()> destroyed_callback_;

    // Handle generation: incremented by 4 for each outgoing request.
    // For Ack/ImmReply, the handle from the most recently received packet is echoed.
    uint32_t next_handle_ = 0;
    uint32_t last_received_handle_ = 0;

    // Peer table: bidirectional mapping between Econet addresses and UDP
    // endpoints -- the resolved routing view, set by the AUN transport.
    // Forward: (net << 8 | stn) -> (ip_addr, port)
    // Reverse: (ip_addr << 16 | port) -> (net, stn)
    std::unordered_map<uint16_t, std::pair<uint32_t, uint16_t>> forward_map_;
    std::unordered_map<uint64_t, std::pair<uint8_t, uint8_t>> reverse_map_;
    mutable std::mutex peer_table_mutex_;

    // Station-number collisions the discovery subscriber rejected (a peer
    // advertised a station already held by a different, still-live endpoint).
    // Surfaced on the Econet status. Guarded separately from the peer table.
    mutable std::mutex collision_mutex_;
    uint32_t collision_count_ = 0;
    std::string last_collision_;

    // Packet trace flag -- set once at construction from BEEBIUM_AUN_TRACE env var.
    bool trace_ = false;

    // Frames dropped because the socket's send buffer was full. Read by
    // diagnostics; a non-zero value means the link is congested enough that
    // the guest is losing traffic it believes it sent.
    uint64_t send_would_block_count_ = 0;

    // Reusable receive buffer (avoids allocation per receive_frame call).
    std::array<uint8_t, 2048> recv_buffer_;

    // Key construction helpers.
    static uint16_t make_forward_key(uint8_t net, uint8_t stn);
    static uint64_t make_reverse_key(uint32_t ip_addr, uint16_t port);

    // Read and discard whatever is queued on the socket, bounded per call.
    // Used while the simulated cable is unplugged so that reconnecting does
    // not deliver a burst of frames from handshakes that have long finished.
    void drain_socket();

    void close_socket();
};

}  // namespace beebium

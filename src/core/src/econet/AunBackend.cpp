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

#include <beebium/econet/AunBackend.hpp>


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <iostream>
#include <span>

namespace beebium {

namespace {

#ifdef _WIN32

// Ensure Winsock is initialized before any socket operations.
// Uses a static object whose constructor calls WSAStartup() exactly once.
struct WinsockInitializer {
    WinsockInitializer() {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            std::cerr << "AunBackend: WSAStartup failed with error " << result << "\n";
        }
    }
    ~WinsockInitializer() {
        WSACleanup();
    }
};

void ensure_winsock_initialized() {
    static WinsockInitializer init;
}

std::string socket_error_string() {
    int err = WSAGetLastError();
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    return buf;
}
#else
std::string socket_error_string() {
    return std::strerror(errno);
}
#endif

}  // anonymous namespace

AunBackend::AunBackend(uint8_t local_net, uint8_t local_stn, uint16_t local_port)
    : local_port_(local_port), local_net_(local_net), local_stn_(local_stn)
    , trace_(std::getenv("BEEBIUM_AUN_TRACE") != nullptr) {

#ifdef _WIN32
    ensure_winsock_initialized();
#endif

    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ == invalid_socket) {
        std::cerr << "AunBackend: socket() failed: " << socket_error_string() << "\n";
        return;
    }

    // Allow rapid restart — avoids "Address already in use" after a crash.
    int reuse = 1;
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse)) < 0) {
        std::cerr << "AunBackend: SO_REUSEADDR failed: " << socket_error_string() << "\n";
        close_socket();
        return;
    }

    // Enable broadcast sends (for sending to all known peers).
    int broadcast = 1;
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char*>(&broadcast), sizeof(broadcast)) < 0) {
        std::cerr << "AunBackend: SO_BROADCAST failed: " << socket_error_string() << "\n";
        close_socket();
        return;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(local_port);

    if (::bind(socket_fd_, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        std::cerr << "AunBackend: bind() to port " << local_port
                  << " failed: " << socket_error_string() << "\n";
        close_socket();
        return;
    }

    // When port 0 is requested, the OS assigns an ephemeral port.
    // Discover the actual port so local_port() reports it correctly.
    if (local_port == 0) {
        sockaddr_in bound_addr{};
        socklen_t bound_len = sizeof(bound_addr);
        if (::getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len) == 0) {
            local_port_ = ntohs(bound_addr.sin_port);
        }
    }

    connected_ = true;
}

AunBackend::~AunBackend() {
    close_socket();
}

void AunBackend::send_frame(const NetworkFrame& frame) {
    if (socket_fd_ == invalid_socket) return;

    // Choose handle: echo for Ack/ImmReply, increment for everything else.
    uint32_t handle;
    if (frame.type == FrameType::Ack || frame.type == FrameType::ImmReply) {
        handle = last_received_handle_;
    } else {
        next_handle_ += 4;
        handle = next_handle_;
    }

    auto packet = aun_packet::encode(frame, handle);

    if (frame.type == FrameType::Broadcast) {
        // Snapshot endpoints under the lock so the actual sendto()
        // calls happen outside the critical section.
        std::vector<std::pair<uint32_t, uint16_t>> endpoints;
        {
            std::lock_guard lock(peer_table_mutex_);
            endpoints.reserve(forward_map_.size());
            for (const auto& [key, endpoint] : forward_map_) {
                endpoints.push_back(endpoint);
            }
        }
        for (const auto& endpoint : endpoints) {
            sockaddr_in dest_addr{};
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_addr.s_addr = endpoint.first;
            dest_addr.sin_port = htons(endpoint.second);

            auto sent = ::sendto(socket_fd_,
                                 reinterpret_cast<const char*>(packet.data()),
                                 static_cast<int>(packet.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&dest_addr),
                                 sizeof(dest_addr));
            if (sent < 0) {
                std::cerr << "AunBackend: sendto() broadcast failed: "
                          << socket_error_string() << "\n";
            }
        }
        return;
    }

    // Unicast / Ack / Immediate / ImmReply / Nack: look up destination peer.
    //
    // BBC software addresses local-segment peers with dest_net=0 (the
    // historical "this network" sentinel from the wire-protocol era).
    // Our peer table is keyed by absolute net numbers, so translate
    // dest_net=0 to local_net_ before the lookup. Frames addressed to a
    // non-zero net (i.e. to another segment) pass through unchanged.
    uint8_t lookup_net = (frame.dest_net == 0) ? local_net_ : frame.dest_net;
    auto forward_key = make_forward_key(lookup_net, frame.dest_stn);
    std::pair<uint32_t, uint16_t> endpoint;
    {
        std::lock_guard lock(peer_table_mutex_);
        auto it = forward_map_.find(forward_key);
        if (it == forward_map_.end()) {
            // Unknown peer -- drop silently.
            return;
        }
        endpoint = it->second;
    }

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = endpoint.first;
    dest_addr.sin_port = htons(endpoint.second);

    auto sent = ::sendto(socket_fd_,
                         reinterpret_cast<const char*>(packet.data()),
                         static_cast<int>(packet.size()), 0,
                         reinterpret_cast<const sockaddr*>(&dest_addr),
                         sizeof(dest_addr));
    if (sent < 0) {
        std::cerr << "AunBackend: sendto() failed: " << socket_error_string() << "\n";
    } else if (trace_) {
        std::cerr << "AUN TX: type=" << static_cast<int>(frame.type)
                  << " port=" << static_cast<int>(frame.port)
                  << " ctrl=" << static_cast<int>(frame.control_byte)
                  << " " << static_cast<int>(frame.src_net) << "." << static_cast<int>(frame.src_stn)
                  << " -> " << static_cast<int>(frame.dest_net) << "." << static_cast<int>(frame.dest_stn)
                  << " data=" << frame.data.size() << "B\n";
    }
}

std::optional<NetworkFrame> AunBackend::receive_frame() {
    if (socket_fd_ == invalid_socket) return std::nullopt;

    // Non-blocking check: is there data waiting?
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_fd_, &readfds);

    timeval timeout{};  // Zero timeout = non-blocking poll
    int ready = ::select(static_cast<int>(socket_fd_ + 1), &readfds, nullptr, nullptr, &timeout);
    if (ready <= 0) return std::nullopt;

    // Read the datagram and capture the sender's address.
    sockaddr_in sender_addr{};
    socklen_t sender_len = sizeof(sender_addr);

    auto received = ::recvfrom(socket_fd_,
                               reinterpret_cast<char*>(recv_buffer_.data()),
                               static_cast<int>(recv_buffer_.size()), 0,
                               reinterpret_cast<sockaddr*>(&sender_addr), &sender_len);
    if (received < 0) {
        std::cerr << "AunBackend: recvfrom() failed: " << socket_error_string() << "\n";
        return std::nullopt;
    }

    if (static_cast<size_t>(received) < AUN_HEADER_SIZE) {
        return std::nullopt;  // Too short — discard
    }

    // Skip self-sends (our own broadcasts looping back).
    uint32_t sender_ip = sender_addr.sin_addr.s_addr;
    uint16_t sender_port = ntohs(sender_addr.sin_port);
    auto reverse_key = make_reverse_key(sender_ip, sender_port);

    // Resolve sender against the peer table once, under the lock, and
    // capture the (net, stn) pair locally so the rest of the function
    // operates on a snapshot.
    std::pair<uint8_t, uint8_t> sender_addr_econet;
    bool sender_known = false;
    {
        std::lock_guard lock(peer_table_mutex_);
        auto it = reverse_map_.find(reverse_key);
        if (it != reverse_map_.end()) {
            sender_known = true;
            sender_addr_econet = it->second;
        }
    }

    if (sender_port == local_port_ && !sender_known) {
        // Unknown sender on our port -- likely a self-send loop, not a
        // real peer. Drop quietly.
        return std::nullopt;
    }

    auto result = aun_packet::decode(
        std::span<const uint8_t>(recv_buffer_.data(), static_cast<size_t>(received)));
    if (!result.valid) {
        return std::nullopt;
    }

    if (!sender_known) {
        return std::nullopt;  // Unknown peer -- discard.
    }

    // Populate addressing from peer table.
    //
    // BBC software expects local-segment frames to carry src_net=0 (the
    // historical wire-protocol sentinel). The peer table holds absolute
    // net numbers; translate the local-segment case back to 0 for the
    // benefit of the BBC. A sender on a genuinely different net keeps its
    // absolute net number, because to the BBC it really is elsewhere.
    //
    // dest_net is always 0, whatever local_net_ is. An inbound frame is by
    // definition addressed to this station, and this station is always on
    // "this segment" from the guest's point of view -- a BBC has no way to
    // learn its own net number, so net 0 is the only form in which it will
    // recognise a frame as its own. Delivering local_net_ here instead makes
    // NFS discard every inbound frame the moment --aun net= is non-zero.
    uint8_t peer_net = sender_addr_econet.first;
    result.frame.src_net = (peer_net == local_net_) ? 0 : peer_net;
    result.frame.src_stn = sender_addr_econet.second;
    result.frame.dest_net = 0;
    result.frame.dest_stn = local_stn_;

    last_received_handle_ = result.handle;

    if (trace_) {
        std::cerr << "AUN RX: type=" << static_cast<int>(result.frame.type)
                  << " port=" << static_cast<int>(result.frame.port)
                  << " ctrl=" << static_cast<int>(result.frame.control_byte)
                  << " " << static_cast<int>(result.frame.src_net) << "." << static_cast<int>(result.frame.src_stn)
                  << " -> " << static_cast<int>(result.frame.dest_net) << "." << static_cast<int>(result.frame.dest_stn)
                  << " data=" << result.frame.data.size() << "B\n";
    }

    return std::move(result.frame);
}

bool AunBackend::is_connected() const {
    return connected_.load(std::memory_order_relaxed);
}

void AunBackend::add_peer(uint8_t net, uint8_t stn, uint32_t ip_addr,
                          uint16_t port, PeerSource source) {
    auto fwd_key = make_forward_key(net, stn);
    std::lock_guard lock(peer_table_mutex_);

    // Operator entries always win: a Discovered call must not
    // overwrite an existing operator-configured entry.
    if (source == PeerSource::Discovered
            && operator_configured_keys_.count(fwd_key) == 1) {
        return;
    }

    // If we're replacing an existing entry, clear its reverse-map
    // mapping first so a stale (ip, port) doesn't keep resolving to
    // this (net, stn) pair after the endpoint changes.
    auto existing = forward_map_.find(fwd_key);
    if (existing != forward_map_.end()) {
        auto old_rev = make_reverse_key(existing->second.first,
                                        existing->second.second);
        reverse_map_.erase(old_rev);
    }

    forward_map_[fwd_key] = {ip_addr, port};
    auto rev_key = make_reverse_key(ip_addr, port);
    reverse_map_[rev_key] = {net, stn};

    if (source == PeerSource::OperatorConfigured) {
        operator_configured_keys_.insert(fwd_key);
    }
}

void AunBackend::remove_peer(uint8_t net, uint8_t stn) {
    auto fwd_key = make_forward_key(net, stn);
    std::lock_guard lock(peer_table_mutex_);
    auto it = forward_map_.find(fwd_key);
    if (it != forward_map_.end()) {
        auto rev_key = make_reverse_key(it->second.first, it->second.second);
        reverse_map_.erase(rev_key);
        forward_map_.erase(it);
        operator_configured_keys_.erase(fwd_key);
    }
}

bool AunBackend::is_operator_configured(uint8_t net, uint8_t stn) const {
    auto fwd_key = make_forward_key(net, stn);
    std::lock_guard lock(peer_table_mutex_);
    return operator_configured_keys_.count(fwd_key) == 1;
}

size_t AunBackend::peer_count() const {
    std::lock_guard lock(peer_table_mutex_);
    return forward_map_.size();
}

uint16_t AunBackend::local_port() const {
    return local_port_;
}

uint8_t AunBackend::local_net() const {
    return local_net_;
}

std::vector<PeerInfo> AunBackend::list_peers() const {
    std::lock_guard lock(peer_table_mutex_);
    std::vector<PeerInfo> result;
    result.reserve(forward_map_.size());
    for (const auto& [key, endpoint] : forward_map_) {
        PeerSource source = (operator_configured_keys_.count(key) == 1)
            ? PeerSource::OperatorConfigured
            : PeerSource::Discovered;
        result.push_back({
            static_cast<uint8_t>(key >> 8),
            static_cast<uint8_t>(key & 0xFF),
            endpoint.first,
            endpoint.second,
            source,
        });
    }
    return result;
}

void AunBackend::set_connected(bool connected) {
    bool prev = connected_.exchange(connected, std::memory_order_relaxed);
    if (prev != connected) {
        bump_backend_status_sequence();
    }
}

uint16_t AunBackend::make_forward_key(uint8_t net, uint8_t stn) {
    return (static_cast<uint16_t>(net) << 8) | stn;
}

uint64_t AunBackend::make_reverse_key(uint32_t ip_addr, uint16_t port) {
    return (static_cast<uint64_t>(ip_addr) << 16) | port;
}

void AunBackend::close_socket() {
    if (socket_fd_ != invalid_socket) {
#ifdef _WIN32
        ::closesocket(socket_fd_);
#else
        ::close(socket_fd_);
#endif
        socket_fd_ = invalid_socket;
    }
    connected_ = false;
}

}  // namespace beebium

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

#include "AunUi.hpp"

#include "AunEconetTransportExtension.hpp"
#include "beebium/econet/AunBackend.hpp"

#include "extension_ui.pb.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <string>

namespace beebium {

namespace {

constexpr const char* CONTROL_CONNECT_ACTION = "connect_action";
constexpr const char* CONTROL_UDP_PORT       = "udp_port";
constexpr const char* CONTROL_PEERS_GROUP    = "peers_group";
constexpr const char* CONTROL_NO_PEERS       = "no_peers";

// Format an IPv4 address (in network byte order, as carried in PeerInfo)
// to dotted-quad. Mirrors the helper in EconetService.hpp's anonymous
// namespace; reproduced here so AunUi doesn't depend on the service
// layer.
std::string format_ip(uint32_t ip_addr) {
    char buf[INET_ADDRSTRLEN];
    in_addr addr;
    addr.s_addr = ip_addr;
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
        return buf;
    }
    return "0.0.0.0";
}

// Compose the per-peer primary label, e.g. "0.254  127.0.0.1:32768".
// Two spaces between the Econet address and the IP endpoint matches
// the hardcoded SwiftUI it replaces. Provenance (operator vs mDNS)
// is carried separately on Label::secondary_text so the renderer can
// style it as a muted caption rather than blending it into the
// primary text.
std::string format_peer_primary(const PeerInfo& peer) {
    std::string out;
    out += std::to_string(static_cast<unsigned>(peer.net));
    out += '.';
    out += std::to_string(static_cast<unsigned>(peer.stn));
    out += "  ";
    out += format_ip(peer.ip_addr);
    out += ':';
    out += std::to_string(peer.port);
    return out;
}

// Caption for the source of a peer. Operator-configured peers get
// no caption (they're the implicit default); discovered peers carry
// "mDNS" so the operator can tell at a glance which entries came
// from auto-discovery vs --aun map= without having to consult the
// typed AunService::ListPeers RPC.
std::string format_peer_secondary(const PeerInfo& peer) {
    if (peer.source == PeerSource::Discovered) return "mDNS";
    return "";
}

}  // namespace

void AunUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* root_group = root->mutable_group();
    root_group->set_label("AUN");

    AunBackend* backend = ext_.backend();

    // Connect / Disconnect action. Single Button whose label flips
    // with the current connection state -- state lives on the
    // transport-agnostic Connection row in NetworkModeView header
    // (driven by EconetService.GetEconetStatus.connected); this
    // button is purely the action affordance for AUN's
    // software-toggleable cable. Stable id "connect_action" so the
    // renderer patches the label in place rather than rebuilding the
    // widget. Suppressed when there's no backend -- no firmware to
    // toggle, and Piconet's connectedness isn't user-actionable
    // either, so the precedent is "no Button when no backend".
    if (backend) {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_CONNECT_ACTION);
        auto* button = control->mutable_button();
        button->set_label(backend->is_connected() ? "Disconnect" : "Connect");
        button->set_enabled(true);
    }

    // Listening port readout. Only AUN exposes a UDP port; this fits
    // nowhere in the transport-agnostic header. Suppressed when there's
    // no backend (port=none or bind failed) -- nothing to report.
    if (backend) {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_UDP_PORT);
        control->mutable_label()->set_text(
            "Listening on UDP port " + std::to_string(backend->local_port()));
    }

    // Peers list (slice 1).
    auto* peers_control = root_group->add_controls();
    peers_control->set_id(CONTROL_PEERS_GROUP);
    auto* peers_group = peers_control->mutable_group();
    peers_group->set_label("Peers");

    if (!backend) {
        auto* no_peers = peers_group->add_controls();
        no_peers->set_id(CONTROL_NO_PEERS);
        // Name the specific fault when we have one (e.g. a bind conflict on a
        // fixed port), so the sidebar leads straight to the cause rather than
        // a generic "unavailable". Falls back to the generic line when the
        // transport is simply off (port=none) or the reason is unknown.
        const std::string& reason = ext_.unavailable_reason();
        no_peers->mutable_label()->set_text(
            reason.empty() ? "AUN backend unavailable" : ("AUN: " + reason));
        return;
    }

    auto peers = backend->list_peers();
    if (peers.empty()) {
        auto* no_peers = peers_group->add_controls();
        no_peers->set_id(CONTROL_NO_PEERS);
        no_peers->mutable_label()->set_text("No peer stations configured");
        return;
    }

    // Stable per-peer ids ("peer.<net>.<stn>") so SwiftUI patches in
    // place if the same peer is updated, and the renderer doesn't
    // rebuild the whole list each push.
    for (const auto& peer : peers) {
        auto* control = peers_group->add_controls();
        control->set_id("peer." + std::to_string(static_cast<unsigned>(peer.net)) +
                        "." + std::to_string(static_cast<unsigned>(peer.stn)));
        auto* label = control->mutable_label();
        label->set_text(format_peer_primary(peer));
        auto secondary = format_peer_secondary(peer);
        if (!secondary.empty()) {
            label->set_secondary_text(std::move(secondary));
        }
    }
}

void AunUi::handle_event(const DispatchRequest& request) {
    // The framework has already validated extension/control/payload;
    // dispatch on control_id alone. Unknown ids reach here only if a
    // future control was added without a matching branch -- silent
    // ignore is the correct fallback (no backend effect).
    if (request.control_id() == CONTROL_CONNECT_ACTION) {
        if (auto* backend = ext_.backend()) {
            backend->set_connected(!backend->is_connected());
            mark_dirty();
        }
    }
    // Slice 3 will add the Add Peer button + TextInput-form-state
    // dispatch handlers here.
}

}  // namespace beebium

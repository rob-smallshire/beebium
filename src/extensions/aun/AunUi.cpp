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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace beebium {

namespace {

constexpr const char* CONTROL_CONNECT_ACTION = "connect_action";
constexpr const char* CONTROL_UDP_PORT       = "udp_port";
constexpr const char* CONTROL_PEERS_GROUP    = "peers_group";
constexpr const char* CONTROL_NO_PEERS       = "no_peers";
constexpr const char* CONTROL_SUBNETS_GROUP  = "subnets_group";
constexpr const char* CONTROL_ADD_PEER       = "add_peer";
constexpr const char* CONTROL_ADD_SUBNET     = "add_subnet";
constexpr const char* CONTROL_RELOAD_MAP     = "reload_map";
constexpr const char* CONTROL_MAP_PATH       = "map_path";
constexpr const char* CONTROL_MAP_LOAD_ERROR = "map_load_error";
constexpr const char* CONTROL_EDIT_ERROR     = "edit_error";

std::string format_ip(std::uint32_t ip_addr) {
    char buf[INET_ADDRSTRLEN];
    in_addr addr;
    addr.s_addr = ip_addr;
    if (inet_ntop(AF_INET, &addr, buf, sizeof(buf))) {
        return buf;
    }
    return "0.0.0.0";
}

std::string provenance_caption(AunPeerProvenance provenance) {
    switch (provenance) {
        case AunPeerProvenance::Launch:     return "launch";
        case AunPeerProvenance::Api:        return "API";
        case AunPeerProvenance::MapFile:    return "map file";
        case AunPeerProvenance::Discovered: return "mDNS";
        case AunPeerProvenance::Subnet:     return "subnet";
    }
    return "";
}

std::string net_stn_text(std::uint8_t net, std::uint8_t stn) {
    return std::to_string(static_cast<unsigned>(net)) + "." +
           std::to_string(static_cast<unsigned>(stn));
}

// Add a TextInput leaf to an editor group.
void add_text_field(Group* editor, const std::string& id,
                    const std::string& label, const std::string& value) {
    auto* control = editor->add_controls();
    control->set_id(id);
    auto* input = control->mutable_text_input();
    input->set_label(label);
    input->set_value(value);
}

// Build a peer Add/Edit form as a ModalEditor: an anchor the renderer turns
// into an edit/add affordance, and an editor tree of TextInputs committed
// atomically. The Add form carries the net.stn field; Edit fixes net.stn (that
// identifies the entry) and edits host/port/label only.
void build_peer_form(Control* control, const std::string& id, bool is_add,
                     const std::string& net_stn, const std::string& host,
                     const std::string& port, const std::string& label) {
    control->set_id(id);
    auto* modal = control->mutable_modal_editor();
    modal->set_editable(true);
    modal->set_commit_role(is_add ? ModalEditor::ADD : ModalEditor::SAVE);
    modal->set_show_cancel(true);
    auto* anchor = modal->mutable_anchor();
    anchor->set_id(id + ".anchor");
    anchor->mutable_label()->set_text(is_add ? "Add peer" : "Edit");
    auto* editor = modal->mutable_editor();
    editor->set_id(id + ".editor");
    auto* group = editor->mutable_group();
    if (is_add) {
        add_text_field(group, id + ".net_stn", "net.stn", net_stn);
    }
    add_text_field(group, id + ".host", "host", host);
    add_text_field(group, id + ".port", "port", port);
    add_text_field(group, id + ".label", "label", label);
}

void build_subnet_form(Control* control, const std::string& id, bool is_add,
                       const std::string& net, const std::string& subnet,
                       const std::string& label) {
    control->set_id(id);
    auto* modal = control->mutable_modal_editor();
    modal->set_editable(true);
    modal->set_commit_role(is_add ? ModalEditor::ADD : ModalEditor::SAVE);
    modal->set_show_cancel(true);
    auto* anchor = modal->mutable_anchor();
    anchor->set_id(id + ".anchor");
    anchor->mutable_label()->set_text(is_add ? "Add subnet" : "Edit");
    auto* editor = modal->mutable_editor();
    editor->set_id(id + ".editor");
    auto* group = editor->mutable_group();
    if (is_add) {
        add_text_field(group, id + ".net", "net", net);
    }
    add_text_field(group, id + ".subnet", "subnet (a.b.c.0/24)", subnet);
    add_text_field(group, id + ".label", "label", label);
}

void add_button(Group* group, const std::string& id, const std::string& label) {
    auto* control = group->add_controls();
    control->set_id(id);
    auto* button = control->mutable_button();
    button->set_label(label);
    button->set_enabled(true);
}

void add_label(Group* group, const std::string& id, const std::string& text,
               const std::string& secondary = "") {
    auto* control = group->add_controls();
    control->set_id(id);
    auto* label = control->mutable_label();
    label->set_text(text);
    if (!secondary.empty()) {
        label->set_secondary_text(secondary);
    }
}

// --- handle_event helpers ---

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The committed value of the editor sub-control whose id ends with `suffix`.
std::optional<std::string> commit_field(const DispatchRequest& request,
                                        std::string_view suffix) {
    for (const auto& field : request.editor_commit().fields()) {
        if (ends_with(field.field_id(), suffix)) {
            return field.string_value();
        }
    }
    return std::nullopt;
}

bool id_after_prefix(const std::string& id, std::string_view prefix,
                     std::string& rest) {
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    rest = id.substr(prefix.size());
    return true;
}

bool parse_net_stn(const std::string& text, int& net, int& stn) {
    auto dot = text.find('.');
    if (dot == std::string::npos) return false;
    try {
        std::size_t net_len = 0, stn_len = 0;
        net = std::stoi(text.substr(0, dot), &net_len);
        stn = std::stoi(text.substr(dot + 1), &stn_len);
        if (net_len != dot || stn_len != text.size() - dot - 1) return false;
    } catch (...) {
        return false;
    }
    return net >= 0 && net <= 255 && stn >= 1 && stn <= 254;
}

bool parse_int(const std::string& text, int& out) {
    try {
        std::size_t len = 0;
        out = std::stoi(text, &len);
        return len == text.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

void AunUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* root_group = root->mutable_group();
    root_group->set_label("AUN");

    AunBackend* backend = ext_.backend();

    // Connect / Disconnect action (see the connect_action comment history).
    if (backend) {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_CONNECT_ACTION);
        auto* button = control->mutable_button();
        button->set_label(backend->is_connected() ? "Disconnect" : "Connect");
        button->set_enabled(true);
    }

    // Listening port readout.
    if (backend) {
        add_label(root_group, CONTROL_UDP_PORT,
                  "Listening on UDP port " +
                      std::to_string(backend->local_port()));
    }

    // Peers group.
    auto* peers_control = root_group->add_controls();
    peers_control->set_id(CONTROL_PEERS_GROUP);
    auto* peers_group = peers_control->mutable_group();
    peers_group->set_label("Peers");

    if (!backend) {
        const std::string& reason = ext_.unavailable_reason();
        add_label(peers_group, CONTROL_NO_PEERS,
                  reason.empty() ? "AUN backend unavailable"
                                 : ("AUN: " + reason));
        return;
    }

    // Map-file peers carry labels and host strings; index them for lookup.
    std::map<std::uint16_t, AunMapPeer> map_by_key;
    for (const auto& p : ext_.map_peers()) {
        map_by_key[(static_cast<std::uint16_t>(p.net) << 8) | p.stn] = p;
    }
    auto map_entry = [&](std::uint8_t net,
                         std::uint8_t stn) -> const AunMapPeer* {
        auto it = map_by_key.find((static_cast<std::uint16_t>(net) << 8) | stn);
        return it == map_by_key.end() ? nullptr : &it->second;
    };

    auto live_peers = ext_.peer_set().list_peers();
    if (live_peers.empty() && ext_.unreachable_map_peers().empty()) {
        add_label(peers_group, CONTROL_NO_PEERS, "No peer stations configured");
    }

    for (const auto& peer : live_peers) {
        const std::string id_suffix = net_stn_text(peer.net, peer.stn);
        auto* control = peers_group->add_controls();
        control->set_id("peer." + id_suffix);
        auto* group = control->mutable_group();

        std::string primary = net_stn_text(peer.net, peer.stn) + "  " +
                              format_ip(peer.ip_addr) + ":" +
                              std::to_string(peer.port);
        const AunMapPeer* file = map_entry(peer.net, peer.stn);
        if (file && !file->label.empty()) {
            primary += "  " + file->label;
        }
        add_label(group, "peer." + id_suffix + ".label", primary,
                  provenance_caption(peer.provenance));

        if (peer.provenance == AunPeerProvenance::MapFile) {
            std::string host = file ? file->host : format_ip(peer.ip_addr);
            std::string label = file ? file->label : std::string{};
            build_peer_form(group->add_controls(), "edit_peer." + id_suffix,
                            /*is_add=*/false, id_suffix, host,
                            std::to_string(peer.port), label);
            add_button(group, "remove_peer." + id_suffix, "Remove");
        } else {
            add_button(group, "save_peer." + id_suffix, "Save to map file");
            if (peer.provenance == AunPeerProvenance::Discovered) {
                add_label(group, "save_peer." + id_suffix + ".warning",
                          "Saving pins this mDNS peer's current (ephemeral) "
                          "port.");
            }
        }
    }

    // Map-file peers whose host did not resolve: shown as unreachable, still
    // editable and removable.
    for (const auto& peer : ext_.unreachable_map_peers()) {
        const std::string id_suffix = net_stn_text(peer.net, peer.stn);
        auto* control = peers_group->add_controls();
        control->set_id("peer." + id_suffix);
        auto* group = control->mutable_group();
        add_label(group, "peer." + id_suffix + ".label",
                  net_stn_text(peer.net, peer.stn) + "  " + peer.host + ":" +
                      std::to_string(peer.port) +
                      (peer.label.empty() ? "" : ("  " + peer.label)) +
                      "  (unreachable)",
                  "map file");
        build_peer_form(group->add_controls(), "edit_peer." + id_suffix,
                        /*is_add=*/false, id_suffix, peer.host,
                        std::to_string(peer.port), peer.label);
        add_button(group, "remove_peer." + id_suffix, "Remove");
    }

    // Add-peer form.
    build_peer_form(peers_group->add_controls(), CONTROL_ADD_PEER,
                    /*is_add=*/true, "", "", "32768", "");

    // Subnet rules group.
    auto* subnets_control = root_group->add_controls();
    subnets_control->set_id(CONTROL_SUBNETS_GROUP);
    auto* subnets_group = subnets_control->mutable_group();
    subnets_group->set_label("Subnet rules");
    for (const auto& subnet : ext_.map_subnets()) {
        const std::string net = std::to_string(static_cast<unsigned>(subnet.net));
        auto* control = subnets_group->add_controls();
        control->set_id("subnet." + net);
        auto* group = control->mutable_group();
        add_label(group, "subnet." + net + ".label",
                  "net " + net + " = " + subnet.subnet_text +
                      ", station = last octet, port 32768",
                  subnet.label);
        build_subnet_form(group->add_controls(), "edit_subnet." + net,
                          /*is_add=*/false, net, subnet.subnet_text,
                          subnet.label);
        add_button(group, "remove_subnet." + net, "Remove");
    }
    build_subnet_form(subnets_group->add_controls(), CONTROL_ADD_SUBNET,
                      /*is_add=*/true, "", "", "");

    // Map file path, load error, edit error, and the reload action.
    std::string map_path = ext_.map_file_path();
    add_label(root_group, CONTROL_MAP_PATH,
              map_path.empty() ? "Map file: (disabled)"
                               : ("Map file: " + map_path));
    if (std::string load_error = ext_.map_file_error(); !load_error.empty()) {
        add_label(root_group, CONTROL_MAP_LOAD_ERROR,
                  "Map file error: " + load_error);
    }
    if (!last_edit_error_.empty()) {
        add_label(root_group, CONTROL_EDIT_ERROR, last_edit_error_);
    }
    add_button(root_group, CONTROL_RELOAD_MAP, "Reload map file");
}

void AunUi::handle_event(const DispatchRequest& request) {
    const std::string& id = request.control_id();

    if (id == CONTROL_CONNECT_ACTION) {
        if (auto* backend = ext_.backend()) {
            backend->set_connected(!backend->is_connected());
            mark_dirty();
        }
        return;
    }

    if (id == CONTROL_RELOAD_MAP) {
        auto result = ext_.reload_map_file();
        last_edit_error_ = result.error;  // empty on success
        mark_dirty();
        return;
    }

    // Add peer: the net.stn field is in the commit.
    if (id == CONTROL_ADD_PEER) {
        auto net_stn = commit_field(request, ".net_stn");
        auto host = commit_field(request, ".host");
        auto port = commit_field(request, ".port");
        auto label = commit_field(request, ".label");
        apply_add_peer(net_stn.value_or(""), host.value_or(""),
                       port.value_or(""), label.value_or(""));
        return;
    }

    std::string rest;
    // Edit peer: net.stn comes from the control id, host/port/label from the
    // commit.
    if (id_after_prefix(id, "edit_peer.", rest)) {
        auto host = commit_field(request, ".host");
        auto port = commit_field(request, ".port");
        auto label = commit_field(request, ".label");
        apply_add_peer(rest, host.value_or(""), port.value_or(""),
                       label.value_or(""));
        return;
    }
    if (id_after_prefix(id, "remove_peer.", rest)) {
        int net = 0, stn = 0;
        if (parse_net_stn(rest, net, stn)) {
            ext_.remove_map_peer(static_cast<std::uint8_t>(net),
                                 static_cast<std::uint8_t>(stn));
            last_edit_error_.clear();
        }
        mark_dirty();
        return;
    }
    if (id_after_prefix(id, "save_peer.", rest)) {
        int net = 0, stn = 0;
        if (parse_net_stn(rest, net, stn)) {
            if (auto endpoint = ext_.peer_set().resolve(
                    static_cast<std::uint8_t>(net),
                    static_cast<std::uint8_t>(stn))) {
                auto result = ext_.add_map_peer(
                    static_cast<std::uint8_t>(net),
                    static_cast<std::uint8_t>(stn), format_ip(endpoint->ip_addr),
                    endpoint->port, "");
                last_edit_error_ = result.error;
            }
        }
        mark_dirty();
        return;
    }
    if (id == CONTROL_ADD_SUBNET) {
        apply_add_subnet(commit_field(request, ".net").value_or(""),
                         commit_field(request, ".subnet").value_or(""),
                         commit_field(request, ".label").value_or(""));
        return;
    }
    if (id_after_prefix(id, "edit_subnet.", rest)) {
        apply_add_subnet(rest, commit_field(request, ".subnet").value_or(""),
                         commit_field(request, ".label").value_or(""));
        return;
    }
    if (id_after_prefix(id, "remove_subnet.", rest)) {
        int net = 0;
        if (parse_int(rest, net) && net >= 0 && net <= 255) {
            ext_.remove_map_subnet(static_cast<std::uint8_t>(net));
            last_edit_error_.clear();
        }
        mark_dirty();
        return;
    }
}

void AunUi::apply_add_peer(const std::string& net_stn, const std::string& host,
                           const std::string& port, const std::string& label) {
    int net = 0, stn = 0;
    if (!parse_net_stn(net_stn, net, stn)) {
        last_edit_error_ = "net.stn must be net.station (net 0-255, station 1-254)";
        mark_dirty();
        return;
    }
    int port_value = 0;
    if (!parse_int(port, port_value) || port_value < 1 || port_value > 65535) {
        last_edit_error_ = "port must be 1-65535";
        mark_dirty();
        return;
    }
    auto result = ext_.add_map_peer(static_cast<std::uint8_t>(net),
                                    static_cast<std::uint8_t>(stn), host,
                                    static_cast<std::uint16_t>(port_value), label);
    last_edit_error_ = result.error;  // empty on success
    mark_dirty();
}

void AunUi::apply_add_subnet(const std::string& net, const std::string& subnet,
                             const std::string& label) {
    int net_value = 0;
    if (!parse_int(net, net_value) || net_value < 0 || net_value > 255) {
        last_edit_error_ = "net must be 0-255";
        mark_dirty();
        return;
    }
    auto result = ext_.add_map_subnet(static_cast<std::uint8_t>(net_value),
                                      subnet, label);
    last_edit_error_ = result.error;
    mark_dirty();
}

}  // namespace beebium

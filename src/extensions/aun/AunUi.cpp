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

namespace beebium {

namespace {

// Control ids addressed by handle_event. The two lists and the file
// reference are the only dispatch targets now; per-item edits arrive as
// EditableListEvents keyed by the item id (the "net.stn" or "net").
constexpr const char* CONTROL_CONNECT_ACTION = "connect_action";
constexpr const char* CONTROL_UDP_PORT       = "udp_port";
constexpr const char* CONTROL_PEERS_LIST     = "peers";
constexpr const char* CONTROL_SUBNETS_LIST   = "subnets";
constexpr const char* CONTROL_MAP_FILE       = "map_file";
constexpr const char* CONTROL_EDIT_ERROR     = "edit_error";

constexpr const char* ACTION_SAVE_TO_MAP = "save_to_map";
constexpr const char* ACTION_RELOAD      = "reload";

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
void add_text_field(Group* group, const std::string& id,
                    const std::string& label, const std::string& value) {
    auto* control = group->add_controls();
    control->set_id(id);
    auto* input = control->mutable_text_input();
    input->set_label(label);
    input->set_value(value);
}

// Build a peer editor tree (host/port/label, and net.stn only when adding)
// into `editor`. Field ids are bare because they are resolved within this one
// editor tree; the editor root takes a distinct id so it cannot shadow a field.
void build_peer_editor(Control* editor, const std::string& editor_id,
                       bool is_add, const std::string& net_stn,
                       const std::string& host, const std::string& port,
                       const std::string& label) {
    editor->set_id(editor_id);
    auto* group = editor->mutable_group();
    if (is_add) {
        add_text_field(group, "net_stn", "net.stn", net_stn);
    }
    add_text_field(group, "host", "host", host);
    add_text_field(group, "port", "port", port);
    add_text_field(group, "label", "label", label);
}

void build_subnet_editor(Control* editor, const std::string& editor_id,
                         bool is_add, const std::string& net,
                         const std::string& subnet, const std::string& label) {
    editor->set_id(editor_id);
    auto* group = editor->mutable_group();
    if (is_add) {
        add_text_field(group, "net", "net", net);
    }
    add_text_field(group, "subnet", "subnet (a.b.c.0/24)", subnet);
    add_text_field(group, "label", "label", label);
}

// --- handle_event helpers ---

// The committed value of the editor field with this exact id, or nullopt.
std::optional<std::string> commit_value(const EditorCommit& commit,
                                        const std::string& field_id) {
    for (const auto& field : commit.fields()) {
        if (field.field_id() == field_id) {
            return field.string_value();
        }
    }
    return std::nullopt;
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

    if (!backend) {
        const std::string& reason = ext_.unavailable_reason();
        auto* control = root_group->add_controls();
        control->set_id("no_peers");
        control->mutable_label()->set_text(
            reason.empty() ? "AUN backend unavailable" : ("AUN: " + reason));
        return;
    }

    // Connect / Disconnect stays a Button -- the one free-standing action.
    {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_CONNECT_ACTION);
        auto* button = control->mutable_button();
        button->set_label(backend->is_connected() ? "Disconnect" : "Connect");
        button->set_enabled(true);
    }

    // Listening port readout.
    {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_UDP_PORT);
        control->mutable_label()->set_text(
            "Listening on UDP port " + std::to_string(backend->local_port()));
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

    // Peers list.
    auto* peers_control = root_group->add_controls();
    peers_control->set_id(CONTROL_PEERS_LIST);
    auto* peers = peers_control->mutable_editable_list();
    peers->set_title("Peers");
    peers->set_can_add(true);
    peers->set_empty_text("No peer stations configured");
    build_peer_editor(peers->mutable_add_editor(), "peers.add_editor",
                      /*is_add=*/true, "", "", "32768", "");

    for (const auto& peer : ext_.peer_set().list_peers()) {
        const std::string id = net_stn_text(peer.net, peer.stn);
        auto* item = peers->add_items();
        item->set_id(id);
        item->set_primary(id + "  " + format_ip(peer.ip_addr) + ":" +
                          std::to_string(peer.port));
        item->set_secondary(provenance_caption(peer.provenance));
        const AunMapPeer* file = map_entry(peer.net, peer.stn);
        if (file && !file->label.empty()) {
            item->set_subtitle(file->label);
        }
        if (peer.provenance == AunPeerProvenance::MapFile) {
            item->set_editable(true);
            item->set_removable(true);
            std::string host = file ? file->host : format_ip(peer.ip_addr);
            std::string label = file ? file->label : std::string{};
            build_peer_editor(item->mutable_editor(), id + ".editor",
                              /*is_add=*/false, id, host,
                              std::to_string(peer.port), label);
        } else {
            auto* action = item->add_actions();
            action->set_id(ACTION_SAVE_TO_MAP);
            action->set_title("Save to map file");
            if (peer.provenance == AunPeerProvenance::Discovered) {
                item->set_note(
                    "Saving pins this mDNS peer's current (ephemeral) port.");
            }
        }
    }

    // Map-file peers whose host did not resolve: shown as unreachable (WARN),
    // still editable and removable.
    for (const auto& peer : ext_.unreachable_map_peers()) {
        const std::string id = net_stn_text(peer.net, peer.stn);
        auto* item = peers->add_items();
        item->set_id(id);
        item->set_primary(id + "  " + peer.host + ":" +
                          std::to_string(peer.port) + "  (unreachable)");
        item->set_secondary(provenance_caption(AunPeerProvenance::MapFile));
        if (!peer.label.empty()) {
            item->set_subtitle(peer.label);
        }
        item->set_state(Indicator_State_WARN);
        item->set_editable(true);
        item->set_removable(true);
        build_peer_editor(item->mutable_editor(), id + ".editor",
                          /*is_add=*/false, id, peer.host,
                          std::to_string(peer.port), peer.label);
    }

    // Subnet rules list.
    auto* subnets_control = root_group->add_controls();
    subnets_control->set_id(CONTROL_SUBNETS_LIST);
    auto* subnets = subnets_control->mutable_editable_list();
    subnets->set_title("Subnet rules");
    subnets->set_can_add(true);
    subnets->set_empty_text("No subnet rules");
    build_subnet_editor(subnets->mutable_add_editor(), "subnets.add_editor",
                        /*is_add=*/true, "", "", "");
    for (const auto& subnet : ext_.map_subnets()) {
        const std::string net = std::to_string(static_cast<unsigned>(subnet.net));
        auto* item = subnets->add_items();
        item->set_id(net);
        item->set_primary("net " + net + " = " + subnet.subnet_text +
                          ", station = last octet, port 32768");
        if (!subnet.label.empty()) {
            item->set_subtitle(subnet.label);
        }
        item->set_editable(true);
        item->set_removable(true);
        build_subnet_editor(item->mutable_editor(), net + ".editor",
                            /*is_add=*/false, net, subnet.subnet_text,
                            subnet.label);
    }

    // The map file itself, as a FileReference: path, state and a Reload action.
    {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_MAP_FILE);
        auto* file = control->mutable_file_reference();
        std::string path = ext_.map_file_path();
        file->set_display_name("aun-map.json");
        std::string load_error = ext_.map_file_error();
        if (path.empty()) {
            file->set_state(Indicator_State_WARN);
            file->set_state_text("Map file disabled");
        } else {
            file->set_path(path);
            if (!load_error.empty()) {
                file->set_state(Indicator_State_ERROR);
                file->set_state_text(load_error);
            } else {
                file->set_state(Indicator_State_OK);
                const std::size_t peers_count = ext_.map_peers().size();
                const std::size_t subnets_count = ext_.map_subnets().size();
                file->set_state_text(
                    std::to_string(peers_count) +
                    (peers_count == 1 ? " peer, " : " peers, ") +
                    std::to_string(subnets_count) +
                    (subnets_count == 1 ? " subnet" : " subnets"));
            }
            auto* action = file->add_actions();
            action->set_id(ACTION_RELOAD);
            action->set_title("Reload");
        }
    }

    // One Indicator for the most recent validation error, shown until the next
    // successful edit clears it.
    if (!last_edit_error_.empty()) {
        auto* control = root_group->add_controls();
        control->set_id(CONTROL_EDIT_ERROR);
        auto* indicator = control->mutable_indicator();
        indicator->set_state(Indicator_State_ERROR);
        indicator->set_text(last_edit_error_);
    }
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

    if (id == CONTROL_MAP_FILE) {
        if (request.file_action_id() == ACTION_RELOAD) {
            auto result = ext_.reload_map_file();
            last_edit_error_ = result.error;  // empty on success
            mark_dirty();
        }
        return;
    }

    if (id == CONTROL_PEERS_LIST) {
        const auto& event = request.editable_list_event();
        const auto& commit = event.commit();
        switch (event.kind()) {
            case EditableListEvent::ADD:
                apply_add_peer(commit_value(commit, "net_stn").value_or(""),
                               commit_value(commit, "host").value_or(""),
                               commit_value(commit, "port").value_or(""),
                               commit_value(commit, "label").value_or(""));
                return;
            case EditableListEvent::EDIT:
                // net.stn is the item id and fixed; host/port/label come from
                // the editor. apply_add_peer replaces the entry in place.
                apply_add_peer(event.item_id(),
                               commit_value(commit, "host").value_or(""),
                               commit_value(commit, "port").value_or(""),
                               commit_value(commit, "label").value_or(""));
                return;
            case EditableListEvent::REMOVE: {
                int net = 0, stn = 0;
                if (parse_net_stn(event.item_id(), net, stn)) {
                    ext_.remove_map_peer(static_cast<std::uint8_t>(net),
                                         static_cast<std::uint8_t>(stn));
                    last_edit_error_.clear();
                }
                mark_dirty();
                return;
            }
            case EditableListEvent::ACTION:
                if (event.action_id() == ACTION_SAVE_TO_MAP) {
                    int net = 0, stn = 0;
                    if (parse_net_stn(event.item_id(), net, stn)) {
                        if (auto endpoint = ext_.peer_set().resolve(
                                static_cast<std::uint8_t>(net),
                                static_cast<std::uint8_t>(stn))) {
                            auto result = ext_.add_map_peer(
                                static_cast<std::uint8_t>(net),
                                static_cast<std::uint8_t>(stn),
                                format_ip(endpoint->ip_addr), endpoint->port,
                                "");
                            last_edit_error_ = result.error;
                        }
                    }
                    mark_dirty();
                }
                return;
            default:
                return;
        }
    }

    if (id == CONTROL_SUBNETS_LIST) {
        const auto& event = request.editable_list_event();
        const auto& commit = event.commit();
        switch (event.kind()) {
            case EditableListEvent::ADD:
                apply_add_subnet(commit_value(commit, "net").value_or(""),
                                 commit_value(commit, "subnet").value_or(""),
                                 commit_value(commit, "label").value_or(""));
                return;
            case EditableListEvent::EDIT:
                // net is the item id and fixed; subnet/label come from the
                // editor.
                apply_add_subnet(event.item_id(),
                                 commit_value(commit, "subnet").value_or(""),
                                 commit_value(commit, "label").value_or(""));
                return;
            case EditableListEvent::REMOVE: {
                int net = 0;
                if (parse_int(event.item_id(), net) && net >= 0 && net <= 255) {
                    ext_.remove_map_subnet(static_cast<std::uint8_t>(net));
                    last_edit_error_.clear();
                }
                mark_dirty();
                return;
            }
            default:
                return;
        }
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

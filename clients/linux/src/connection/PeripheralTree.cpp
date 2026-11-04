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

#include "connection/PeripheralTree.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <set>

namespace beebium::linux_client {

namespace {

std::string humanised(const std::string& slug) {
    std::string text = slug;
    std::replace(text.begin(), text.end(), '-', ' ');
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

// The server folds the fallbacks into the label; the id or the manifest
// name stand in only where it sent none.
std::string name_of(const beebium::ExtensionInfo& info) {
    if (!info.label().empty() && info.label() != info.id()) {
        return info.label();
    }
    return !info.id().empty() ? info.id() : humanised(info.name());
}

}  // namespace

PeripheralTree peripheral_tree(const beebium::ListExtensionsResponse& response) {
    PeripheralTree tree;
    tree.listing = response;
    std::map<std::string, std::string> point_names;
    for (const auto& point : response.attachment_points()) {
        point_names[point.id()] =
            point.display_name().empty() ? point.id() : point.display_name();
    }
    std::set<std::string> listed;
    for (const auto& info : response.extensions()) {
        listed.insert(info.id());
    }

    std::map<std::string, std::vector<const beebium::ExtensionInfo*>> roots_by_point;
    std::map<std::string, std::vector<const beebium::ExtensionInfo*>> children_by_provider;
    std::vector<const beebium::ExtensionInfo*> unattached;
    for (const auto& info : response.extensions()) {
        if (info.attachments().empty()) {
            unattached.push_back(&info);
            continue;
        }
        const auto& primary = info.attachments(0);
        if (primary.provider_id().empty()) {
            roots_by_point[primary.point()].push_back(&info);
        } else if (listed.count(primary.provider_id())) {
            children_by_provider[primary.provider_id()].push_back(&info);
        } else {
            unattached.push_back(&info);  // its provider is not listed: still shown
        }
    }

    std::set<std::string> placed;
    std::function<PeripheralNode(const beebium::ExtensionInfo&)> build =
        [&](const beebium::ExtensionInfo& info) {
            placed.insert(info.id());
            PeripheralNode node;
            node.id = info.id();
            node.name = name_of(info);
            node.description = info.description();
            node.has_ui = info.has_ui();
            for (int i = 1; i < info.attachments_size(); ++i) {
                const auto& point = info.attachments(i).point();
                node.also.push_back(point_names.count(point) ? point_names[point] : point);
            }
            for (const auto* child : children_by_provider[info.id()]) {
                if (!placed.count(child->id())) {
                    node.children.push_back(build(*child));
                }
            }
            return node;
        };

    for (const auto& point : response.attachment_points()) {
        const auto found = roots_by_point.find(point.id());
        if (found == roots_by_point.end()) {
            continue;  // nothing on it
        }
        PeripheralGroup group;
        group.point_id = point.id();
        group.display_name = point_names[point.id()];
        for (const auto* info : found->second) {
            group.nodes.push_back(build(*info));
        }
        tree.groups.push_back(std::move(group));
        roots_by_point.erase(found);
    }
    // Roots on a point the server did not describe, then the unattached.
    for (const auto& [point_id, infos] : roots_by_point) {
        PeripheralGroup group;
        group.point_id = point_id;
        group.display_name = point_id;
        for (const auto* info : infos) {
            group.nodes.push_back(build(*info));
        }
        tree.groups.push_back(std::move(group));
    }
    for (const auto* info : unattached) {
        tree.unattached.push_back(build(*info));
    }
    return tree;
}

std::vector<PeripheralStorageDevice> PeripheralTree::storage_devices() const {
    std::map<std::string, const beebium::ExtensionInfo*> by_id;
    for (const auto& info : listing.extensions()) {
        by_id[info.id()] = &info;
    }
    std::vector<PeripheralStorageDevice> devices;
    std::function<void(const PeripheralNode&)> visit = [&](const PeripheralNode& node) {
        if (const auto found = by_id.find(node.id); found != by_id.end()) {
            for (const auto& device : found->second->storage_devices()) {
                devices.push_back({device.id(), device.name(), device.backing_path(),
                                   device.activity_indicator_name()});
            }
        }
        for (const auto& child : node.children) {
            visit(child);
        }
    };
    for (const auto& group : groups) {
        for (const auto& node : group.nodes) {
            visit(node);
        }
    }
    for (const auto& node : unattached) {
        visit(node);
    }
    return devices;
}

void SerialLine::observe(const beebium::SerialStatus& status) {
    socket_ = status.has_serial_socket();
    connector_ = status.connector();
    if (status.rs423_selected()) {
        rs423_ = std::make_pair(status.tx_baud(), status.rx_baud());
    }
}

std::string SerialLine::text() const {
    if (!socket_) {
        return {};
    }
    const std::string connector = connector_.empty() ? std::string("RS423") : connector_;
    if (!rs423_) {
        return connector;
    }
    return connector + " Tx " + std::to_string(rs423_->first) + " / Rx " +
           std::to_string(rs423_->second) + " baud";
}

}  // namespace beebium::linux_client

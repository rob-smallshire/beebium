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

#ifndef BEEBIUM_LINUX_CONNECTION_PERIPHERAL_TREE_HPP
#define BEEBIUM_LINUX_CONNECTION_PERIPHERAL_TREE_HPP

#include "peripheral_extension.pb.h"
#include "serial.pb.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace beebium::linux_client {

// The machine's peripherals as the server lists them (client specification
// 12.3), as the macOS client arranges them. Nothing here knows any
// particular extension.

struct PeripheralNode {
    std::string id;
    std::string name;         // the server's label for this instance
    std::string description;  // the manifest's, for a tooltip
    bool has_ui = false;
    // The display names of the other points it is attached to: a device
    // on several ports shows once, under its first.
    std::vector<std::string> also;
    std::vector<PeripheralNode> children;  // attached to it as their provider
};

// The extensions attached to one of the machine's own connectors (or
// another point a root provides), in the server's order.
struct PeripheralGroup {
    std::string point_id;
    std::string display_name;
    std::vector<PeripheralNode> nodes;
};

struct PeripheralStorageDevice {
    std::string id;
    std::string name;
    std::string backing_path;        // on the server's host; empty for none
    std::string activity_indicator;  // the indicator to watch, if any
};

struct PeripheralTree {
    // Only points with an extension on them, in the server's point order.
    std::vector<PeripheralGroup> groups;
    // Attached nowhere, or to a provider the server did not list.
    std::vector<PeripheralNode> unattached;

    bool empty() const { return groups.empty() && unattached.empty(); }
    // Every storage device the peripherals publish, in tree order.
    std::vector<PeripheralStorageDevice> storage_devices() const;

    // The listing, kept for the storage devices.
    beebium::ListExtensionsResponse listing;
};

PeripheralTree peripheral_tree(const beebium::ListExtensionsResponse& response);

// The on-board serial interface's line: its connector and, once seen with
// RS423 selected, the RS423 rates. The Serial ULA shares its rate with
// the cassette, whose rate is never shown, so the last RS423 pair stands.
class SerialLine {
public:
    void observe(const beebium::SerialStatus& status);
    // Empty when there is no serial socket, or nothing heard yet.
    std::string text() const;

private:
    bool socket_ = false;
    std::string connector_;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> rs423_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_CONNECTION_PERIPHERAL_TREE_HPP

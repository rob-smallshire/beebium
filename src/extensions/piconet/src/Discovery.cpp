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

#include "beebium/econet/piconet/Discovery.hpp"

#include "beebium/econet/piconet/Constants.hpp"

#include <string>
#include <vector>

namespace beebium::piconet {

namespace {

std::string join_paths(const std::vector<std::string>& paths) {
    std::string out;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i != 0) out += ", ";
        out += paths[i];
    }
    return out;
}

}  // namespace

DiscoveryResult discover_piconet_device(const std::string& explicit_path,
                                        const PortEnumerator& enumerate,
                                        const PortProber& probe) {
    // An explicit path pins the device: honour it exactly, no discovery.
    if (!explicit_path.empty() && explicit_path != AUTO_DEVICE_PATH) {
        return {true, explicit_path,
                "Using configured device_path " + explicit_path};
    }

    const auto ports = enumerate();

    // Narrow to ports whose USB vendor id is the Raspberry Pi Pico's. Ports
    // with no reported USB identity cannot be confirmed as the Pico and are
    // excluded so the STATUS probe never touches an unrelated device.
    std::vector<std::string> vid_candidates;
    for (const auto& port : ports) {
        if (port.usb_vendor_id && *port.usb_vendor_id == USB_VENDOR_ID) {
            vid_candidates.push_back(port.path);
        }
    }

    // Probe each candidate; STATUS is read-only and state-preserving.
    std::vector<std::string> confirmed;
    for (const auto& path : vid_candidates) {
        if (probe(path)) {
            confirmed.push_back(path);
        }
    }

    if (confirmed.size() == 1) {
        return {true, confirmed.front(),
                "Discovered Piconet at " + confirmed.front()};
    }

    if (confirmed.empty()) {
        std::string message;
        if (vid_candidates.empty()) {
            message = "No Piconet found: no serial port with the Raspberry Pi "
                      "Pico USB vendor id (0x2E8A) is present among " +
                      std::to_string(ports.size()) +
                      " enumerated serial port(s). Attach the Piconet, or pass "
                      "device_path=<path> to select a device explicitly.";
        } else {
            message = "No Piconet found: " +
                      std::to_string(vid_candidates.size()) +
                      " candidate port(s) with USB vendor 0x2E8A (" +
                      join_paths(vid_candidates) +
                      ") did not answer a Piconet STATUS probe. Pass "
                      "device_path=<path> to select a device explicitly.";
        }
        return {false, {}, std::move(message)};
    }

    return {false, {},
            "Multiple Piconet devices found (" + join_paths(confirmed) +
                "). Pass device_path=<path> to choose one."};
}

}  // namespace beebium::piconet

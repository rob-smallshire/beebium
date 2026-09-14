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

// Pluralise "serial port" for a count.
std::string ports_phrase(std::size_t count) {
    return std::to_string(count) +
           (count == 1 ? " serial port" : " serial ports");
}

// Defensive cap on how many VID-matching ports we probe, so a host that
// somehow presents a long list of Pico-VID devices can never make one
// discovery pass take unboundedly long (each probe blocks up to its
// timeout). Real hosts have one or two.
constexpr std::size_t MAX_PROBE_CANDIDATES = 8;

}  // namespace

DiscoveryResult discover_piconet_device(const std::string& explicit_path,
                                        const PortEnumerator& enumerate,
                                        const PortProber& probe) {
    // An explicit path pins the device: honour it exactly, no discovery.
    if (!explicit_path.empty() && explicit_path != AUTO_DEVICE_PATH) {
        DiscoveryResult result;
        result.ok = true;
        result.device_path = explicit_path;
        result.message = "Using configured device_path " + explicit_path;
        result.ui_message = "Device: " + explicit_path;
        return result;
    }

    const auto ports = enumerate();

    // Narrow to ports whose USB vendor id is the Raspberry Pi Pico's. Ports
    // with no reported USB identity cannot be confirmed as the Pico and are
    // excluded so the STATUS probe never touches an unrelated device.
    std::vector<std::string> vid_candidates;
    for (const auto& port : ports) {
        if (port.usb_vendor_id && *port.usb_vendor_id == USB_VENDOR_ID) {
            vid_candidates.push_back(port.path);
            if (vid_candidates.size() >= MAX_PROBE_CANDIDATES) break;
        }
    }

    // Probe each candidate; STATUS is read-only and state-preserving.
    std::vector<std::string> confirmed;
    for (const auto& path : vid_candidates) {
        if (probe(path)) {
            confirmed.push_back(path);
        }
    }

    DiscoveryResult result;
    result.ports_checked = ports.size();

    if (confirmed.size() == 1) {
        result.ok = true;
        result.device_path = confirmed.front();
        result.message = "Discovered Piconet at " + confirmed.front();
        result.ui_message = "Piconet at " + confirmed.front();
        return result;
    }

    if (confirmed.empty()) {
        if (vid_candidates.empty()) {
            result.message =
                "No Piconet found: no serial port with the Raspberry Pi "
                "Pico USB vendor id (0x2E8A) is present among " +
                ports_phrase(ports.size()) +
                ". Attach the Piconet, or pass device_path=<path> to select a "
                "device explicitly.";
        } else {
            result.message =
                "No Piconet found: " + std::to_string(vid_candidates.size()) +
                " candidate port(s) with USB vendor 0x2E8A (" +
                join_paths(vid_candidates) +
                ") did not answer a Piconet STATUS probe. Pass "
                "device_path=<path> to select a device explicitly.";
        }
        // Concise, GUI-friendly: no device_path advice (CLI-only), and the
        // count reflects how many host serial ports were considered.
        result.ui_message = "No Piconet found (" + ports_phrase(ports.size()) +
                            " checked). Attach a Piconet and retry.";
        return result;
    }

    result.message = "Multiple Piconet devices found (" + join_paths(confirmed) +
                     "). Pass device_path=<path> to choose one.";
    result.ui_message = "Multiple Piconets found (" +
                        std::to_string(confirmed.size()) +
                        "). Leave one attached and retry.";
    return result;
}

}  // namespace beebium::piconet

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

// Resolve which serial device a Piconet transport should open when the user
// did not pin one down. The Raspberry Pi Pico the firmware runs on
// enumerates with stock TinyUSB descriptors (VID 0x2E8A, PID 0x000A, no
// "Piconet" marker), so USB metadata only narrows the field; a STATUS probe
// makes the positive identification. See Probe.hpp.
//
// The decision is split from I/O behind two seams -- a port enumerator and a
// per-path prober -- so the one/zero/many + VID-filter logic is unit-tested
// server-free. PiconetEconetTransportExtension wires the real
// beebium::serial::enumerate_serial_ports() and a probe that opens the port
// and calls probe_status().

#include "beebium/serial/EnumeratePorts.hpp"

#include <functional>
#include <string>
#include <vector>

namespace beebium::piconet {

// Sentinel device_path value that requests discovery explicitly (in
// addition to an absent/empty value).
inline constexpr const char* AUTO_DEVICE_PATH = "auto";

// Outcome of resolving a device path.
struct DiscoveryResult {
    bool ok = false;          // true iff device_path is usable
    std::string device_path;  // the resolved path when ok
    std::string message;      // human-readable detail (success or error), always set
};

// Enumerate host serial ports (with USB identity). Injected for testing.
using PortEnumerator = std::function<std::vector<serial::SerialPortInfo>()>;

// Probe one port path: return true iff it is confirmed to be a Piconet.
// Injected for testing; production opens the port and runs probe_status().
using PortProber = std::function<bool(const std::string& path)>;

// Resolve the device path for a Piconet transport.
//
//   * explicit_path non-empty and not "auto": returned verbatim (ok=true),
//     preserving today's behaviour -- no enumeration, no probing.
//   * otherwise: enumerate, keep ports whose USB vendor id is the Pico's
//     (0x2E8A), probe each, and decide:
//       - exactly one confirmed  -> ok=true, device_path set, message names it
//       - zero confirmed         -> ok=false, message names what was searched
//       - more than one confirmed-> ok=false, message lists them
DiscoveryResult discover_piconet_device(const std::string& explicit_path,
                                        const PortEnumerator& enumerate,
                                        const PortProber& probe);

}  // namespace beebium::piconet

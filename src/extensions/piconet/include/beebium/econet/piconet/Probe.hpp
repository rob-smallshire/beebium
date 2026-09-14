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

// Protocol probe used to positively identify a Piconet device on an open
// serial port. USB metadata (VID 0x2E8A) only narrows the field of
// candidate ports; the Raspberry Pi Pico enumerates with stock TinyUSB
// descriptors that carry no "Piconet" marker, so identification is by
// PROTOCOL: send STATUS and check for a well-formed STATUS reply.
//
// The probe is read-only and state-preserving -- STATUS reports the
// firmware version, station and mode without changing them -- so it is
// safe to run against a live network-attached device, and a device with
// no Econet network attached still answers.

#include "beebium/econet/piconet/Events.hpp"
#include "beebium/econet/piconet/SerialPort.hpp"

#include <chrono>
#include <optional>

namespace beebium::piconet {

// Send STATUS to an already-open serial port and wait up to `timeout` for
// a STATUS reply. Returns the parsed StatusEvent when the device
// identifies itself as a Piconet; std::nullopt otherwise: the port is not
// open, the write fails, the device stays silent until the deadline, or it
// answers with something that is not a STATUS line (a different serial
// device that happens to share the USB VID).
//
// Non-STATUS event lines seen before the deadline are skipped rather than
// treated as a rejection: a Piconet left in LISTEN/MONITOR mode may emit
// RX_*/MONITOR traffic interleaved with the STATUS reply. Only reaching
// the deadline with no STATUS line seen is a negative result.
std::optional<StatusEvent> probe_status(
    SerialPort& serial,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

}  // namespace beebium::piconet

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

#include "PiconetEconetTransportExtension.hpp"

#ifdef BEEBIUM_BUILD_SERVICE
#include "PiconetDispatcher.hpp"
#endif
#include "beebium/econet/piconet/Discovery.hpp"
#include "beebium/econet/piconet/PiconetConfig.hpp"
#include "beebium/econet/piconet/Probe.hpp"
#include "beebium/serial/EnumeratePorts.hpp"

#ifdef _WIN32
#include "beebium/econet/piconet/Win32SerialPort.hpp"
#else
#include "beebium/econet/piconet/PosixSerialPort.hpp"
#endif

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

namespace beebium {

namespace {

// Single platform-selection helper used both for the initial open in
// create_backend and for the SerialFactory the backend uses to build
// a replacement during process_pending_reopen. Keeping the #ifdef in
// one place ensures the two paths can never drift to different
// SerialPort implementations.
std::unique_ptr<piconet::SerialPort> make_platform_serial(
    const std::string& path)
{
#ifdef _WIN32
    return std::make_unique<piconet::Win32SerialPort>(path);
#else
    return std::make_unique<piconet::PosixSerialPort>(path);
#endif
}

}  // namespace

PiconetEconetTransportExtension::PiconetEconetTransportExtension() = default;
PiconetEconetTransportExtension::~PiconetEconetTransportExtension() = default;

std::vector<ExtensionRpcDispatcher*>
PiconetEconetTransportExtension::rpc_dispatchers() {
#ifdef BEEBIUM_BUILD_SERVICE
    if (!dispatcher_) {
        dispatcher_ = std::make_unique<PiconetDispatcher>(*this);
    }
    return {dispatcher_.get()};
#else
    return {};
#endif
}

std::unique_ptr<NetworkBackend>
PiconetEconetTransportExtension::create_backend(std::uint8_t station) {
    // device_path is optional. An explicit path is honoured exactly; an
    // absent path (or the sentinel "auto") triggers discovery: enumerate
    // serial ports, keep the Raspberry Pi Pico's USB vendor id, and probe
    // each with a STATUS handshake (see Discovery.hpp / Probe.hpp).
    auto configured = config_value("device_path");
    std::string explicit_path(configured ? *configured : std::string_view{});

    auto discovery = piconet::discover_piconet_device(
        explicit_path,
        [] { return serial::enumerate_serial_ports(); },
        [](const std::string& candidate) {
            auto candidate_serial = make_platform_serial(candidate);
            if (!candidate_serial->is_open()) {
                return false;
            }
            return piconet::probe_status(*candidate_serial,
                                         std::chrono::milliseconds(500))
                .has_value();
        });

    if (!discovery.ok) {
        // No usable device. Returning nullptr lets install_econet install a
        // disconnected backend so the machine still boots ("no network") --
        // exactly what a preset booted on a Piconet-less host needs.
        std::cerr << "Piconet extension: " << discovery.message << "\n";
        open_error_message_ = discovery.message;
        return nullptr;
    }
    std::cout << "Piconet extension: " << discovery.message << "\n";

    std::string path(discovery.device_path);
    auto serial = make_platform_serial(path);
    if (!serial->is_open()) {
        // Log the failure but still construct a PiconetBackend in a
        // closed state. The ModalEditor on the Piconet panel needs a
        // live backend to call request_reopen() against; without this,
        // a wrong-path-at-startup is unrecoverable from the UI. The
        // backend's own open_error_message() carries the OS-level
        // diagnosis that PiconetUi surfaces on the Indicator; the
        // extension's open_error_message_ (used for the "no
        // device_path configured" case below) stays empty.
        std::cerr << "Piconet extension: failed to open device " << path
                  << ": " << serial->open_error() << "\n";
    }

    // Wire the backend's async-state-change hook to the UI's
    // mark_dirty(). Without this the panel View stays frozen across
    // hot-unplug events -- the reader thread closes the serial port
    // but the framework's poll loop only sees a revision change when
    // mark_dirty() is called. Captures `this` because the extension
    // owns both ui_ and the backend (via the unique_ptr handed to
    // EconetSocket); the callback's lifetime is bounded by both.
    auto backend = std::make_unique<PiconetBackend>(
        piconet::PiconetConfig{path, station},
        std::move(serial),
        &make_platform_serial,
        [this]{ ui_.mark_dirty(); });
    backend_ = backend.get();  // non-owning; ownership goes to EconetSocket
    open_error_message_.clear();
    return backend;
}

}  // namespace beebium

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

piconet::DiscoveryResult PiconetEconetTransportExtension::run_discovery() {
    // device_path is optional. An explicit path is honoured exactly; an
    // absent path (or the sentinel "auto") triggers discovery: enumerate
    // serial ports, keep the Raspberry Pi Pico's USB vendor id, and probe
    // each with a STATUS handshake (see Discovery.hpp / Probe.hpp).
    auto configured = config_value("device_path");
    std::string explicit_path(configured ? *configured : std::string_view{});

    return piconet::discover_piconet_device(
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
}

std::unique_ptr<NetworkBackend>
PiconetEconetTransportExtension::create_backend(std::uint8_t station) {
    auto discovery = run_discovery();

    if (!discovery.ok) {
        // No usable device. Log the verbose diagnostic verbatim to the
        // CLI/boot log, keep the concise version for the GUI Indicator, and
        // still return a DISCONNECTED backend: the machine boots with no
        // network (ADLC reports no clock, exactly as before) AND the UI has
        // a live backend to run Retry against. serial=nullptr keeps
        // open_error_message_ empty so the Indicator shows discovery_status_
        // rather than an OS errno. The SerialFactory is wired so Retry's
        // request_reopen can bring the device up in place.
        std::cerr << "Piconet extension: " << discovery.message << "\n";
        open_error_message_.clear();
        discovery_status_ = discovery.ui_message;
        auto backend = std::make_unique<PiconetBackend>(
            piconet::PiconetConfig{std::string{}, station},
            /*serial=*/nullptr,
            &make_platform_serial,
            [this]{ ui_.mark_dirty(); });
        backend_ = backend.get();
        return backend;
    }
    std::cout << "Piconet extension: " << discovery.message << "\n";
    discovery_status_.clear();

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

void PiconetEconetTransportExtension::retry_discovery() {
    // No backend means create_backend was never called (no Econet fitted);
    // nothing to retry against.
    if (!backend_) {
        return;
    }

    auto discovery = run_discovery();
    if (discovery.ok) {
        // Bring the device up live in place: request_reopen hands the open
        // and reader-restart to the emulation thread (ownership-safe), and
        // Listen leaves the station connected in this one action.
        std::cout << "Piconet extension: " << discovery.message << "\n";
        discovery_status_.clear();
        backend_->request_reopen(discovery.device_path, piconet::Mode::Listen);
    } else {
        // Still nothing usable; refresh the concise status (verbose to log).
        std::cerr << "Piconet extension: " << discovery.message << "\n";
        discovery_status_ = discovery.ui_message;
    }
}

}  // namespace beebium

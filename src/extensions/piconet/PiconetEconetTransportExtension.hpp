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

#ifndef BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP
#define BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP

// Plugin EconetTransportExtension for the Piconet USB-CDC bridge to a
// real Econet wire. POSIX-only (PosixSerialPort is the only SerialPort
// implementation today). Constructed by plugin_entry.cpp via the
// generic beebium_create_extension ABI; the framework hands us our
// config map (parsed from --piconet device_path=/dev/ttyX or the
// equivalent preset entry), and create_backend() opens the device
// and constructs a PiconetBackend wrapping it.

#include "PiconetUi.hpp"
#include "beebium/econet/PiconetBackend.hpp"
#include "beebium/econet/piconet/Discovery.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace beebium {

class ExtensionRpcDispatcher;
#ifdef BEEBIUM_BUILD_SERVICE
class PiconetDispatcher;  // forward; defined when the service layer is built
#endif

class PiconetEconetTransportExtension : public EconetTransportExtension {
public:
    PiconetEconetTransportExtension();
    ~PiconetEconetTransportExtension() override;

    // Construct a PiconetBackend from the current config. ALWAYS returns a
    // backend: with an explicit device_path (or a successful discovery) it
    // wraps the opened device; otherwise it returns a DISCONNECTED
    // PiconetBackend (no serial) so the machine still boots with no network
    // AND the UI has a live backend to run Retry against. install_econet
    // reports the connected/unavailable state truthfully from
    // backend->is_connected().
    //
    // The constructed backend is also stashed as a non-owning pointer
    // so PiconetUi can read its state. The owning unique_ptr is handed
    // off to EconetSocket; the raw pointer mirrors the AUN extension's
    // pattern and shares its lifetime caveat (becomes dangling at
    // machine shutdown, which only happens at process exit).
    std::unique_ptr<NetworkBackend> create_backend(std::uint8_t station) override;

    // Re-run discovery on demand (the UI "Retry" action) and, on success,
    // bring the existing backend live at the discovered device without
    // restarting the machine -- via the backend's ownership-safe
    // request_reopen (the open/reader-restart happens on the emulation
    // thread). On failure the concise discovery status is refreshed. Safe
    // to call from the gRPC/dispatch thread.
    void retry_discovery();

    // Concise, GUI-facing discovery status (e.g. "No Piconet found (1
    // serial port checked). Attach a Piconet and retry."). Empty once a
    // device is connected or an explicit device_path is configured. The
    // verbose diagnostic still goes to the CLI/boot log verbatim.
    const std::string& discovery_status() const { return discovery_status_; }

    // Piconet bridges to a real, physical Econet line with real stations
    // running in wall time, so its protocol timing only interleaves correctly
    // when the emulation runs at real time. See docs/networking.md ("Emulation
    // speed and real-time peers").
    bool requires_real_time_pacing() const override { return true; }

    // Non-owning accessor used by PiconetUi.
    PiconetBackend* backend() { return backend_; }
    const PiconetBackend* backend() const { return backend_; }

    // Empty unless create_backend has been called and failed at the
    // device-open step. PiconetUi reads this to surface a human-readable
    // OS-level diagnosis ("No such file or directory", "Permission
    // denied", etc) on the Indicator's text field, rather than just
    // showing the generic "Adapter offline".
    const std::string& open_error_message() const { return open_error_message_; }

    // ExtensionUi hook: returns a stable pointer to the per-extension
    // PiconetUi. The framework reads from the View tree it produces and
    // dispatches validated events into its handle_event.
    ExtensionUi* ui() override { return &ui_; }

    // Piconet-specific status (PiconetService.GetStatus), served through the
    // core's ExtensionRpc channel rather than a plugin-hosted gRPC service.
    std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() override;

private:
    // Build the enumerator + prober seams and run discovery against the
    // current device_path config. Shared by create_backend (boot) and
    // retry_discovery (UI action) so both audiences see identical logic.
    piconet::DiscoveryResult run_discovery();

    PiconetBackend* backend_ = nullptr;  // non-owning; lives in EconetSocket
    std::string open_error_message_;
    std::string discovery_status_;  // concise GUI status; see discovery_status()
    PiconetUi ui_{*this};
#ifdef BEEBIUM_BUILD_SERVICE
    std::unique_ptr<PiconetDispatcher> dispatcher_;  // lazily constructed
#endif
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSIONS_PICONET_PICONET_ECONET_TRANSPORT_EXTENSION_HPP

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

// Vtable anchor for EconetTransportExtension and EconetTransportRegistry.
//
// Both types are declared with BEEBIUM_EXT_API (dllimport on Windows when
// consumed from outside beebium_extension_api). MSVC requires the vtable
// for a dllimport-decorated polymorphic class to live in the exporting
// DLL, which means at least one virtual function must have a non-inline
// definition there. These out-of-line definitions provide that anchor.

#include "beebium/extension/EconetTransportExtension.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"

namespace beebium {

EconetTransportExtension::~EconetTransportExtension() = default;

void EconetTransportExtension::on_station_id_changed(uint8_t /*new_id*/) {}

bool EconetTransportExtension::requires_real_time_pacing() const { return false; }

EconetTransportExtension::AutoStationOutcome
EconetTransportExtension::select_auto_station(econet::StationRange /*range*/) {
    AutoStationOutcome outcome;
    outcome.status = AutoStationOutcome::Status::Unsupported;
    outcome.report =
        "automatic station selection (--station auto) is not supported by this "
        "transport; give an explicit station number";
    return outcome;
}

void EconetTransportExtension::arm_backend_destroyed(
    NetworkBackend* backend, std::function<void()> teardown) {
    // A fresh token per arm: an earlier backend's callback, should its
    // destruction be deferred past a re-Enable that armed a new backend, finds
    // this expired and does nothing (so does the identity check the teardown
    // makes). The weak capture makes the callback a no-op once the extension is
    // gone, whatever order the extension and a reader-co-owned backend tear down.
    backend_destroyed_token_ = std::make_shared<bool>(true);
    std::weak_ptr<bool> alive = backend_destroyed_token_;
    backend->set_destroyed_callback(
        [alive, teardown = std::move(teardown)]() {
            auto keep_alive = alive.lock();
            if (!keep_alive) {
                return;  // the extension is gone
            }
            teardown();
        });
}

EconetTransportRegistry::~EconetTransportRegistry() = default;

}  // namespace beebium

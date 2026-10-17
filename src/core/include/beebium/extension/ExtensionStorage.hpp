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

#ifndef BEEBIUM_EXTENSION_EXTENSION_STORAGE_HPP
#define BEEBIUM_EXTENSION_EXTENSION_STORAGE_HPP

#include "Export.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

// Server-side description of a single storage device published by an
// extension. The fields mirror StorageDevice in peripheral_extension.proto;
// PeripheralExtensionServiceImpl serialises a vector of these into the
// proto response when populating ExtensionInfo.
//
// Activity LEDs do not flow through this struct -- they continue to be
// published via Indicators / IndicatorService. activity_indicator_name
// just names the entry the frontend should subscribe to.
struct StorageDeviceInfo {
    enum class Kind {
        Fixed,      // non-removable; no eject affordance
        Removable,  // user-swappable media; future UIs add mount/eject
    };

    std::string id;
    std::string name;          // storage-context display name; see proto comment
    Kind kind = Kind::Fixed;
    std::string media_type;             // "hard-disc", "floppy", "ram-disc", ...
    std::string backing_path;           // image filepath (or empty)
    std::string activity_indicator_name;
};

// Capability mixin for extensions that publish storage devices. An
// extension declares it offers storage by multiply-inheriting this
// alongside its primary extension-point base (PeripheralExtension,
// EconetTransportExtension, ...) and overriding Extension::storage()
// to return `this`. The framework discovers storage-bearing extensions
// via that accessor -- no dynamic_cast, no RTTI dependency across
// plugin boundaries.
//
// Mirrors the ExtensionUi pattern (capability surface as a separate
// abstract base, opted into via a virtual accessor on Extension).
class BEEBIUM_EXT_TYPE_VISIBLE ExtensionStorage {
public:
    BEEBIUM_EXT_API virtual ~ExtensionStorage();

    // Snapshot of devices the extension currently publishes. Called
    // by the framework when serialising ExtensionInfo; should be
    // cheap. One extension may publish zero or more devices
    // (vanishingly few publish zero in practice, since the whole
    // point of opting in is to publish at least one).
    virtual std::vector<StorageDeviceInfo> devices() const = 0;

    // Re-point the device `device_id` at `filepath`, a copy of its image
    // already made, holding everything written so far: close the image it
    // has open and open that one in its place, so later writes land there
    // and devices() reports it. Used when a running machine is saved and its
    // media move with it. Called with emulation paused. Returns false, with
    // `error` saying why and the device left on its old image, when it
    // cannot; the default cannot.
    BEEBIUM_EXT_API virtual bool reopen(std::string_view device_id,
                                        const std::filesystem::path& filepath,
                                        std::string& error);
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_EXTENSION_STORAGE_HPP

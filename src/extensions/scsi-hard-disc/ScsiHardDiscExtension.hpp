// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SCSI_HARD_DISC_EXTENSION_HPP
#define BEEBIUM_SCSI_HARD_DISC_EXTENSION_HPP

#include "ScsiHardDiscUi.hpp"

#include <beebium/extension/ExtensionStorage.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace beebium {

class ScsiHardDisc;

// Peripheral extension that attaches a SCSI hard disc to an Acorn SCSI
// host adapter. Discovers the adapter via the "scsi" extension point
// and installs a ScsiHardDisc target in its target registry.
//
// The disc image filepath and SCSI target ID must be configured before
// init() via the config map (keys "image" and "scsi-id") or the legacy
// set_image_filepath() / set_scsi_id() setters. Hard discs are fixed
// media: there is no facility to mount, swap, or eject at runtime.
// If no image is configured the extension initialises but installs no
// target.
class ScsiHardDiscExtension : public PeripheralExtension,
                              public ExtensionStorage {
public:
    ScsiHardDiscExtension();
    ~ScsiHardDiscExtension() override;

    static std::unique_ptr<ScsiHardDiscExtension> create();

    // Configuration (call before init)
    void set_image_filepath(std::filesystem::path filepath) { image_filepath_ = std::move(filepath); }
    void set_scsi_id(uint8_t id) { scsi_id_ = id; }

    // PeripheralExtension
    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"scsi"};
        return deps;
    }

    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // Fold the SCSI target ID into the default label so that multiple
    // drives on the same adapter render distinguishably in the
    // Peripherals sidebar. "SCSI" is omitted from the type word
    // because the row sits under the parent SCSI Bus section already.
    std::string default_label() const override;

    // ExtensionUi panel for the Peripherals sidebar. Returns a stable
    // pointer (owned by this extension) showing the disc capacity.
    ExtensionUi* ui() override { return &ui_; }

    // Storage capability: publishes this drive to the Storage
    // sidebar. One device per extension instance.
    ExtensionStorage* storage() override { return this; }

    // ExtensionStorage interface
    std::vector<StorageDeviceInfo> devices() const override;
    bool reopen(std::string_view device_id, const std::filesystem::path& filepath,
                std::string& error) override;

    // Geometry + state of the loaded disc image. Captured by init()
    // from HardDiskImage at open time; remain at defaults if no image
    // was configured. Public setters so tests can drive the UI
    // without standing up a full SCSI adapter and on-disk image.
    uint32_t total_sectors() const { return total_sectors_; }
    void set_total_sectors(uint32_t n) { total_sectors_ = n; }

    uint16_t cylinders() const { return cylinders_; }
    void set_cylinders(uint16_t n) { cylinders_ = n; }

    uint8_t heads() const { return heads_; }
    void set_heads(uint8_t n) { heads_ = n; }

    // True when the host filesystem permissions on the DAT file
    // disallow writes. The Peripherals sidebar surfaces this as a
    // "Read-only" Indicator on the disc's panel.
    bool is_write_protected() const { return write_protected_; }
    void set_write_protected(bool b) { write_protected_ = b; }

private:
    std::filesystem::path image_filepath_;
    uint8_t scsi_id_ = 0;
    uint32_t total_sectors_ = 0;
    uint16_t cylinders_ = 0;
    uint8_t heads_ = 0;
    bool write_protected_ = false;
    std::unique_ptr<ScsiHardDisc> disc_;
    ScsiHardDisc* target_ = nullptr;  // the installed target, owned by the adapter's registry
    PeripheralExtension* adapter_ = nullptr;  // non-owning, from context
    ScsiHardDiscUi ui_{*this};
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HARD_DISC_EXTENSION_HPP

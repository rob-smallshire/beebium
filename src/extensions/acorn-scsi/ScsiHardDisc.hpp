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

#ifndef BEEBIUM_SCSI_HARD_DISC_HPP
#define BEEBIUM_SCSI_HARD_DISC_HPP

#include "HardDiskImage.hpp"
#include "ScsiTarget.hpp"

#include <memory>
#include <string>
#include <vector>

namespace beebium {

// SCSI target implementing a hard disc backed by a DAT+DSC image file.
// Handles all SCSI commands that a direct-access device responds to.
class ScsiHardDisc : public ScsiTarget {
public:
    explicit ScsiHardDisc(std::unique_ptr<HardDiskImage> image);

    // ScsiTarget interface
    ScsiCommandResult execute(std::span<const uint8_t> cdb,
                              std::span<const uint8_t> data_out) override;
    bool is_present() const override { return image_ != nullptr; }
    std::string_view device_type() const override { return "hard-disc"; }
    std::string_view description() const override { return description_; }

    // Access the underlying image (for testing)
    HardDiskImage* image() { return image_.get(); }
    const HardDiskImage* image() const { return image_.get(); }

    // Serve `image` in place of the image open now, which is closed. Its
    // sense data is kept: to the initiator it is the same disc.
    void replace_image(std::unique_ptr<HardDiskImage> image) {
        image_ = std::move(image);
        description_ = "SCSI hard disc";
        if (image_) description_ += " (" + image_->dat_filepath().filename().string() + ")";
    }

private:
    // Command handlers
    ScsiCommandResult handle_test_unit_ready();
    ScsiCommandResult handle_request_sense(std::span<const uint8_t> cdb);
    ScsiCommandResult handle_format_unit();
    ScsiCommandResult handle_read_6(std::span<const uint8_t> cdb);
    ScsiCommandResult handle_write_6(std::span<const uint8_t> cdb,
                                     std::span<const uint8_t> data_out);
    ScsiCommandResult handle_translate(std::span<const uint8_t> cdb);
    ScsiCommandResult handle_inquiry(std::span<const uint8_t> cdb);
    ScsiCommandResult handle_mode_select_6(std::span<const uint8_t> cdb,
                                           std::span<const uint8_t> data_out);
    ScsiCommandResult handle_mode_sense_6(std::span<const uint8_t> cdb);
    ScsiCommandResult handle_read_capacity();
    ScsiCommandResult handle_read_10(std::span<const uint8_t> cdb);
    ScsiCommandResult handle_write_10(std::span<const uint8_t> cdb,
                                      std::span<const uint8_t> data_out);
    ScsiCommandResult handle_verify_10(std::span<const uint8_t> cdb);

    // Helpers
    ScsiCommandResult read_sectors(uint32_t lba, uint32_t count);
    ScsiCommandResult write_sectors(uint32_t lba, uint32_t count,
                                    std::span<const uint8_t> data);
    void set_sense(uint8_t key, uint8_t asc = 0, uint8_t ascq = 0, uint32_t info = 0);
    ScsiCommandResult check_condition();
    ScsiCommandResult good();

    std::unique_ptr<HardDiskImage> image_;
    std::string description_;

    // Sense data (cleared on REQUEST SENSE per SCSI spec)
    std::vector<uint8_t> sense_data_;
};

}  // namespace beebium

#endif  // BEEBIUM_SCSI_HARD_DISC_HPP

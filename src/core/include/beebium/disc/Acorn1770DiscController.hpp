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

#include "DiscControllerInterface.hpp"
#include "DiscDrive.hpp"
#include "WD1770.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace beebium {

// Acorn 1770 DFS disc controller for BBC Model B
//
// This implements the Acorn 1770 DFS upgrade board which was a common
// aftermarket addition for the BBC Model B. The same layout is used
// in the built-in disc controller on the Model B+.
//
// Address mapping (within socket region 0xFE80-0xFE9F):
//
//   Offset 0x00-0x03: Control register (write-only, reads as 0xFF)
//     Bit 0: Drive 0 select (active high)
//     Bit 1: Drive 1 select (active high)
//     Bit 2: Side select (0=side 0, 1=side 1)
//     Bit 3: Density (0=double/MFM, 1=single/FM)
//     Bit 4: Motor on (ignored - WD1770 handles motor internally)
//     Bit 5: Reset (active low, falling edge triggers reset)
//     Bit 6: NMI enable (tracked but not gated - see note below)
//     Bit 7: Unused
//
//   Offset 0x04-0x07: WD1770 registers
//     0x04: Status (read) / Command (write)
//     0x05: Track register
//     0x06: Sector register
//     0x07: Data register
//
//   Offset 0x08-0x1F: Returns 0xFF (open bus)
//
// Note on NMI enable (bit 6): On real hardware, this bit gates the combined
// DRQ/INTRQ signal before it reaches the CPU's NMI line. Following B2's
// approach, we track but don't actually gate the NMI - DFS relies on this
// behavior and timing is critical for sector transfers.
//
class Acorn1770DiscController : public DiscControllerInterface {
public:
    Acorn1770DiscController() = default;
    ~Acorn1770DiscController() override = default;

    // Non-copyable, non-movable (contains WD1770 which is non-copyable)
    Acorn1770DiscController(const Acorn1770DiscController&) = delete;
    Acorn1770DiscController& operator=(const Acorn1770DiscController&) = delete;
    Acorn1770DiscController(Acorn1770DiscController&&) = delete;
    Acorn1770DiscController& operator=(Acorn1770DiscController&&) = delete;

    // =========================================================================
    // MemoryMappedDevice interface
    // =========================================================================

    uint8_t read(uint16_t offset) override {
        offset &= 0x1F;  // Mask to 32-byte region

        if (offset < 0x04) {
            // Control register is write-only, returns 0xFF (open bus)
            // This is how DFS detects 1770 vs 8271 - the 8271 has readable
            // status at this address
            return 0xFF;
        }

        if (offset < 0x08) {
            // WD1770 registers (mirrored in 0x04-0x07)
            return wd1770_.read(offset - 0x04);
        }

        // Rest of region (0x08-0x1F) is open bus
        return 0xFF;
    }

    void write(uint16_t offset, uint8_t value) override {
        offset &= 0x1F;

        if (offset < 0x04) {
            // Control register (mirrored in 0x00-0x03)
            write_control(value);
            return;
        }

        if (offset < 0x08) {
            // WD1770 registers (mirrored in 0x04-0x07)
            wd1770_.write(offset - 0x04, value);
            return;
        }

        // Writes to 0x08-0x1F are ignored
    }

    // =========================================================================
    // Clock interface
    // =========================================================================

    void tick() override {
        wd1770_.tick();
    }

    // =========================================================================
    // NMI source interface
    // =========================================================================

    bool nmi_pending() const override {
        // Following B2's approach, we don't gate NMI with the enable bit.
        // DFS relies on this behavior for correct timing.
        return wd1770_.nmi_pending();
    }

    // =========================================================================
    // Reset
    // =========================================================================

    void reset() override {
        wd1770_.reset();
        control_ = 0;
        nmi_enabled_ = false;
    }

    // =========================================================================
    // Drive management
    // =========================================================================

    void attach_drive(int drive_num, DiscDrive* drive) override {
        if (drive_num >= 0 && drive_num < 2) {
            drives_[static_cast<size_t>(drive_num)] = drive;
            wd1770_.attach_drive(drive_num, drive);
        }
    }

    void detach_drives() override {
        drives_[0] = nullptr;
        drives_[1] = nullptr;
        wd1770_.attach_drive(0, nullptr);
        wd1770_.attach_drive(1, nullptr);
    }

    DiscDrive* attached_drive(int drive_num) const override {
        if (drive_num < 0 || drive_num > 1) {
            return nullptr;
        }
        return drives_[static_cast<size_t>(drive_num)];
    }

    // =========================================================================
    // Identification
    // =========================================================================

    std::string_view name() const override {
        return "Acorn 1770";
    }

    std::string_view fdc_chip() const override {
        return wd1770_.name();
    }

    // =========================================================================
    // Configuration
    // =========================================================================

    void set_spin_up_delay_enabled(bool enabled) override {
        wd1770_.set_spin_up_delay_enabled(enabled);
    }

    bool spin_up_delay_enabled() const override {
        return wd1770_.spin_up_delay_enabled();
    }

    // =========================================================================
    // Acorn-specific accessors (for testing)
    // =========================================================================

    // Get current control register value
    uint8_t control() const { return control_; }

    // Check if NMI is enabled (bit 6 of control register)
    bool nmi_enabled() const { return nmi_enabled_; }

    // Direct access to WD1770 for testing
    WD1770& wd1770() { return wd1770_; }
    const WD1770& wd1770() const { return wd1770_; }

private:
    void write_control(uint8_t value) {
        uint8_t old_control = control_;
        control_ = value;

        // Bits 0/1: Drive select (one-hot encoding)
        // DFS uses bits 0-1 for drive select, not as a binary number
        if (value & 0x01) {
            wd1770_.set_drive(0);
        } else if (value & 0x02) {
            wd1770_.set_drive(1);
        }
        // If neither bit set, drive selection is indeterminate

        // Bit 2: Side select (propagated to both FDC and drives)
        uint8_t side = (value & 0x04) ? 1 : 0;
        wd1770_.set_side(side);
        if (drives_[0]) drives_[0]->set_side(side);
        if (drives_[1]) drives_[1]->set_side(side);

        // Bit 3: Density (0=double/MFM, 1=single/FM)
        wd1770_.set_density((value & 0x08) == 0);

        // Bit 4: Motor on (for 8271-style explicit control)
        // Note: WD1770 handles motor internally via spin_up()/spin_down()
        // during command execution, so this bit is effectively ignored.

        // Bit 5: Reset (active low)
        // Reset is active when bit 5 is LOW (0), inactive when HIGH (1)
        // Only reset on the falling edge (transition from 1 to 0)
        bool reset_active = (value & 0x20) == 0;
        bool was_reset_active = (old_control & 0x20) == 0;
        if (reset_active && !was_reset_active) {
            wd1770_.reset();
        }

        // Bit 6: NMI enable
        nmi_enabled_ = (value & 0x40) != 0;
    }

    WD1770 wd1770_;
    std::array<DiscDrive*, 2> drives_ = {nullptr, nullptr};
    uint8_t control_ = 0;
    bool nmi_enabled_ = false;
};

} // namespace beebium

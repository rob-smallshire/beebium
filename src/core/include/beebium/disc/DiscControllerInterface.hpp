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

#include <cstdint>
#include <string_view>

namespace beebium {

// Forward declaration
class DiscDrive;

// Abstract interface for disc controllers (WD1770, 8271, etc.)
//
// This interface is used by DiscControllerSocket to provide pluggable
// disc controller support for the Model B emulator. Different controller
// implementations (Acorn 1770, Opus, Watford, Intel 8271) can be installed
// at runtime.
//
// The controller is responsible for:
// - Memory-mapped register access (within its region of the socket's 32-byte window)
// - 1MHz peripheral clock handling
// - NMI generation (for DRQ/INTRQ signals)
// - Drive selection and attachment
//
// Drives are owned by the hardware (not the controller), allowing disc images
// to persist across controller changes.
//
class DiscControllerInterface {
public:
    virtual ~DiscControllerInterface() = default;

    // =========================================================================
    // Memory-mapped device interface
    // =========================================================================

    // Read from controller register
    // @param offset Offset within the socket region (0x00-0x1F)
    // @return Register value, or 0xFF for unmapped/write-only registers
    virtual uint8_t read(uint16_t offset) = 0;

    // Write to controller register
    // @param offset Offset within the socket region (0x00-0x1F)
    // @param value Value to write
    virtual void write(uint16_t offset, uint8_t value) = 0;

    // =========================================================================
    // Clock interface
    // =========================================================================

    // Advance controller state by one 1MHz tick
    // Called from poll_nmi() after sampling NMI state
    virtual void tick() = 0;

    // =========================================================================
    // NMI source interface
    // =========================================================================

    // Check if NMI is pending (DRQ or INTRQ active)
    // @return true if NMI should be asserted
    virtual bool nmi_pending() const = 0;

    // =========================================================================
    // Reset
    // =========================================================================

    // Reset controller to power-on state
    virtual void reset() = 0;

    // =========================================================================
    // Drive management
    // =========================================================================

    // Attach a drive to the controller
    // @param drive_num Drive number (0 or 1)
    // @param drive Pointer to drive (owned by hardware, not controller)
    virtual void attach_drive(int drive_num, DiscDrive* drive) = 0;

    // Detach all drives from the controller
    // Called before removing controller from socket
    virtual void detach_drives() = 0;

    // Query attached drive
    // @param drive_num Drive number (0 or 1)
    // @return Pointer to attached drive, or nullptr if not attached
    virtual DiscDrive* attached_drive(int drive_num) const = 0;

    // =========================================================================
    // Identification
    // =========================================================================

    // Get controller name for display/logging
    // @return Controller name (e.g., "Acorn 1770", "Opus DDOS")
    virtual std::string_view name() const = 0;

    // The floppy controller chip's short name, from the device itself
    // ("WD1770"; "8271" for an 8271 board). Shown by the fdc-controller name
    // placeholder.
    virtual std::string_view fdc_chip() const = 0;

    // =========================================================================
    // Configuration (for testing/automation)
    // =========================================================================

    // Enable/disable motor spin-up delay
    // When disabled, commands execute immediately without waiting for motor
    // @param enabled true to enable delays (realistic), false to skip (fast)
    virtual void set_spin_up_delay_enabled(bool enabled) = 0;

    // Check if spin-up delay is enabled
    virtual bool spin_up_delay_enabled() const = 0;

protected:
    // Non-copyable, non-movable (prevents slicing)
    DiscControllerInterface() = default;
    DiscControllerInterface(const DiscControllerInterface&) = delete;
    DiscControllerInterface& operator=(const DiscControllerInterface&) = delete;
    DiscControllerInterface(DiscControllerInterface&&) = delete;
    DiscControllerInterface& operator=(DiscControllerInterface&&) = delete;
};

} // namespace beebium

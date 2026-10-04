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
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace beebium {

// Forward declaration
class DiscDrive;

// Disc controller socket for BBC Model B
//
// This class represents the physical socket on the Model B motherboard where
// disc controller hardware can be installed. It sits in the memory map at
// 0xFE80-0xFE9F and delegates to an installed controller, or returns open bus
// (0xFF) when empty.
//
// The socket satisfies the MemoryMappedDevice concept and can be registered
// directly in the memory map:
//
//   make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(disc_socket)
//
// Usage:
//   DiscControllerSocket socket;
//   socket.install(std::make_unique<Acorn1770DiscController>());
//   socket.attach_drive(0, &drive_0);
//   socket.attach_drive(1, &drive_1);
//
// When no controller is installed:
//   - read() returns 0xFF (open bus)
//   - write() is ignored
//   - nmi_pending() returns false
//   - tick() does nothing
//
class DiscControllerSocket {
public:
    // Construct an empty socket
    DiscControllerSocket() = default;

    // Non-copyable and non-movable: it owns the controller, and other threads
    // read its recorded chip name under a mutex.
    DiscControllerSocket(const DiscControllerSocket&) = delete;
    DiscControllerSocket& operator=(const DiscControllerSocket&) = delete;
    DiscControllerSocket(DiscControllerSocket&&) = delete;
    DiscControllerSocket& operator=(DiscControllerSocket&&) = delete;

    ~DiscControllerSocket() = default;

    // =========================================================================
    // MemoryMappedDevice interface
    // =========================================================================

    // Read from disc controller region
    // @param offset Address offset (0x00-0x1F within the socket's region)
    // @return Register value from controller, or 0xFF if socket is empty
    uint8_t read(uint16_t offset) {
        if (!controller_) {
            return 0xFF;  // Open bus when no controller installed
        }
        return controller_->read(offset);
    }

    // Write to disc controller region
    // @param offset Address offset (0x00-0x1F within the socket's region)
    // @param value Value to write
    void write(uint16_t offset, uint8_t value) {
        if (controller_) {
            controller_->write(offset, value);
        }
        // Writes to empty socket are silently ignored
    }

    // =========================================================================
    // Clock interface
    // =========================================================================

    // Advance controller state by one 1MHz tick
    // Call this after sampling NMI state
    void tick() {
        if (controller_) {
            controller_->tick();
        }
    }

    // =========================================================================
    // NMI source interface
    // =========================================================================

    // Check if NMI is pending from disc controller
    // @return true if controller is installed and has NMI pending
    bool nmi_pending() const {
        if (!controller_) {
            return false;  // Empty socket generates no NMI
        }
        return controller_->nmi_pending();
    }

    // =========================================================================
    // Controller management
    // =========================================================================

    // Install a disc controller into the socket
    // Any previously installed controller is removed first
    // @param controller Controller to install (takes ownership)
    void install(std::unique_ptr<DiscControllerInterface> controller) {
        // Detach drives from old controller before replacing
        if (controller_) {
            controller_->detach_drives();
        }
        controller_ = std::move(controller);
        set_fdc_chip(controller_ ? std::string(controller_->fdc_chip()) : std::string());
    }

    // Remove and return the installed controller
    // @return Previously installed controller, or nullptr if socket was empty
    std::unique_ptr<DiscControllerInterface> remove() {
        if (controller_) {
            controller_->detach_drives();
        }
        set_fdc_chip({});
        return std::move(controller_);
    }

    // The fitted controller's chip name ("WD1770"), or empty with the socket
    // empty. Safe from any thread: the controller can be installed or removed
    // at runtime (DiscService, with the emulation thread parked), so a server
    // thread reads this copy, recorded at install and removal, rather than
    // the controller itself. The emulation thread's per-cycle path never
    // touches it.
    std::string fdc_chip() const {
        std::lock_guard<std::mutex> lock(fdc_chip_mutex_);
        return fdc_chip_;
    }

    // Check if a controller is installed
    // @return true if a controller is present
    bool has_controller() const {
        return controller_ != nullptr;
    }

    // Access the installed controller
    // @return Pointer to controller, or nullptr if socket is empty
    DiscControllerInterface* controller() {
        return controller_.get();
    }

    // Access the installed controller (const)
    const DiscControllerInterface* controller() const {
        return controller_.get();
    }

    // =========================================================================
    // Drive management (convenience, delegates to controller)
    // =========================================================================

    // Attach a drive to the installed controller
    // No-op if no controller is installed
    // @param drive_num Drive number (0 or 1)
    // @param drive Pointer to drive (owned by hardware)
    void attach_drive(int drive_num, DiscDrive* drive) {
        if (controller_) {
            controller_->attach_drive(drive_num, drive);
        }
    }

    // Query attached drive
    // Returns nullptr if no controller installed or drive not attached
    // @param drive_num Drive number (0 or 1)
    // @return Pointer to attached drive, or nullptr
    DiscDrive* attached_drive(int drive_num) const {
        if (!controller_) {
            return nullptr;
        }
        return controller_->attached_drive(drive_num);
    }

    // =========================================================================
    // Reset
    // =========================================================================

    // Reset the installed controller
    // No-op if no controller is installed
    void reset() {
        if (controller_) {
            controller_->reset();
        }
    }

private:
    void set_fdc_chip(std::string chip) {
        std::lock_guard<std::mutex> lock(fdc_chip_mutex_);
        fdc_chip_ = std::move(chip);
    }

    std::unique_ptr<DiscControllerInterface> controller_;
    mutable std::mutex fdc_chip_mutex_;
    std::string fdc_chip_;
};

} // namespace beebium

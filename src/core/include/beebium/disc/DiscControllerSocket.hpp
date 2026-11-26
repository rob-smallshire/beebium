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
#include "beebium/indicators/Indicators.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace beebium {

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
// A controller brings two drives (#292): the socket owns them, and creates
// them, with their FDD 0 and FDD 1 activity LEDs, when a controller is first
// installed. An empty socket has no drives and so no drive LEDs.
//
// Usage:
//   DiscControllerSocket socket{indicators};
//   socket.install(std::make_unique<Acorn1770DiscController>());
//   socket.attached_drive(0)->insert(...);
//
// When no controller is installed:
//   - read() returns 0xFF (open bus)
//   - write() is ignored
//   - nmi_pending() returns false
//   - tick() does nothing
//   - attached_drive() returns nullptr
//
class DiscControllerSocket {
public:
    // Construct an empty socket whose drives, once a controller brings them,
    // have no activity LEDs.
    DiscControllerSocket() = default;

    // Construct an empty socket whose drives, once a controller brings them,
    // register their activity LEDs with `indicators`.
    explicit DiscControllerSocket(Indicators& indicators)
        : indicators_(&indicators) {}

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

    // Read for inspection, without the controller's read side effects
    uint8_t peek(uint16_t offset) const {
        return controller_ ? controller_->peek(offset) : 0xFF;
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

    // Install a disc controller into the socket, connected to the socket's
    // two drives. The first install creates the drives, registering their
    // activity LEDs, so it must come before the indicators are sealed. Any
    // previously installed controller is removed first; the drives, and any
    // discs in them, stay. Installing nullptr empties the socket.
    // @param controller Controller to install (takes ownership)
    void install(std::unique_ptr<DiscControllerInterface> controller) {
        if (controller_) {
            controller_->detach_drives();
        }
        controller_ = std::move(controller);
        set_fdc_chip(controller_ ? std::string(controller_->fdc_chip()) : std::string());
        if (controller_) {
            if (!drives_[0]) {
                create_drives();
            }
            controller_->attach_drive(0, drives_[0].get());
            controller_->attach_drive(1, drives_[1].get());
        }
    }

    // Remove and return the installed controller. The drives are no longer
    // reachable (attached_drive() is nullptr), but an LED, once registered,
    // stays registered: removal is for a machine not yet started, and no
    // assembly path fits a controller and then takes it out.
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
    // Drives
    // =========================================================================

    // The drive the fitted controller brings, the one way to reach a drive.
    // @param drive_num Drive number (0 or 1)
    // @return The drive, or nullptr with the socket empty or drive_num not 0 or 1
    DiscDrive* attached_drive(int drive_num) const {
        if (!controller_ || drive_num < 0 || drive_num > 1) {
            return nullptr;
        }
        return drives_[static_cast<size_t>(drive_num)].get();
    }

    // =========================================================================
    // Reset
    // =========================================================================

    // Reset the installed controller
    // No-op if no controller is installed
    void reset(ResetCause cause) {
        if (controller_) {
            controller_->reset(cause);
        }
    }

private:
    void create_drives() {
        if (indicators_) {
            drives_[0] = std::make_unique<DiscDrive>(*indicators_, "floppy-0-activity-led", "FDD 0", "568nm");
            drives_[1] = std::make_unique<DiscDrive>(*indicators_, "floppy-1-activity-led", "FDD 1", "568nm");
        } else {
            drives_[0] = std::make_unique<DiscDrive>();
            drives_[1] = std::make_unique<DiscDrive>();
        }
    }

    void set_fdc_chip(std::string chip) {
        std::lock_guard<std::mutex> lock(fdc_chip_mutex_);
        fdc_chip_ = std::move(chip);
    }

    Indicators* indicators_ = nullptr;
    // Declared before the controller, so destroyed after it: the controller
    // holds pointers to them.
    std::array<std::unique_ptr<DiscDrive>, 2> drives_;
    std::unique_ptr<DiscControllerInterface> controller_;
    mutable std::mutex fdc_chip_mutex_;
    std::string fdc_chip_;
};

} // namespace beebium

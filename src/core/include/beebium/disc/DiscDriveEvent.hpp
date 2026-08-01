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
#include <functional>
#include <string>

namespace beebium {

// Something that happened to a drive, reported as it happens.
//
// A drive announces its own changes rather than leaving observers to notice
// them: sampling drive state periodically cannot see an excursion that begins
// and ends between two samples, and an eject followed straight away by an
// insert is exactly such an excursion.
enum class DiscDriveEventType {
    Inserted,        // A disc was placed in the drive
    EjectRequested,  // A safe eject began; waiting for the motor to stop
    EjectCancelled,  // A pending safe eject was abandoned; the disc stays
    Ejected,         // The disc left after the drive fell quiet
    ForceEjected,    // The disc was pulled out without waiting
    MotorOn,
    MotorOff,
};

// A drive event, carrying everything an observer needs to describe it.
//
// The disc fields are a snapshot taken at the moment of the change, not a
// reference to be read later: by the time an observer runs on another thread
// the drive may hold something else, or nothing.
struct DiscDriveEvent {
    // Most events are the type alone, so name it and let the rest default
    // rather than spelling out empty disc details at every raising point.
    explicit DiscDriveEvent(DiscDriveEventType type) : type(type) {}

    DiscDriveEventType type;

    // Populated for Inserted; empty otherwise.
    std::string source_url;
    std::string disc_name;
    std::string format;
    uint32_t sides = 1;
    bool write_protected = false;
};

// Called by a drive at the instant it changes, on whichever thread made the
// change: the emulation thread for motor transitions and for a safe eject
// completing, an RPC thread for an insert or an immediate eject.
//
// The contract is that an observer returns promptly and never blocks. It is
// called with the drive mid-transition, so it must not call back into the
// drive.
using DiscDriveObserver = std::function<void(const DiscDriveEvent&)>;

} // namespace beebium

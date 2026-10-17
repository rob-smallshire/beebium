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

// A piece of hardware whose contents a battery keeps while the machine is
// switched off: board RAM, a clock's CMOS. A machine lists its units
// (Machine::battery_units); every unit can be given the contents it has when
// its battery is first fitted, and can give up and take back its contents, so
// that a saved machine keeps them from one run to the next.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace beebium {

class BatteryBackedUnit {
public:
    virtual ~BatteryBackedUnit() = default;

    // What the unit's saved contents are called: a short, stable,
    // file-name-safe name, unique within a machine ("integra-b-rtc").
    virtual std::string_view name() const = 0;

    // The version of the layout serialise() gives. Contents saved in another
    // layout are not restored.
    virtual std::uint32_t layout_version() const = 0;

    // The hardware the contents belong to, as text. Contents saved for other
    // hardware (another board, another firmware) are not restored.
    virtual std::string hardware_descriptor() const = 0;

    // Give the unit the contents it has when its battery is first fitted:
    // what a machine starts with until it has saved any, and what a reset of
    // the battery returns it to.
    virtual void seed() = 0;

    // The unit's contents.
    virtual std::vector<std::uint8_t> serialise() const = 0;

    // Take back contents serialise() gave. Returns false, changing nothing,
    // when `contents` is not something this unit can hold.
    virtual bool restore(std::span<const std::uint8_t> contents) = 0;
};

}  // namespace beebium

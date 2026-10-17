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

#ifndef BEEBIUM_EXTENSION_EXTENSION_PERSISTENCE_HPP
#define BEEBIUM_EXTENSION_EXTENSION_PERSISTENCE_HPP

#include "Export.hpp"

#include <beebium/BatteryBackedUnit.hpp>

#include <vector>

namespace beebium {

// Capability mixin for an extension whose device keeps state while the
// machine is switched off, as a battery keeps a clock running: the state a
// saved machine carries from one run to the next. An extension opts in by
// multiply-inheriting this and overriding Extension::persistence() to return
// `this`, as for storage() and ui().
//
// The state is given as battery-backed units, the surface the core's own
// hardware uses (BatteryBackedUnit), and the server keeps it: for an
// instance of a saved machine, in extensions/<instance id>/nvram/ of the
// machine directory, one file per unit. It restores the units once the
// instance is initialised and before the machine runs (a unit with nothing
// kept keeps what init() gave it, which is its seed), keeps them at a
// graceful shutdown and on Save, and forgets them on a reset of the battery.
// Contents saved in another layout, or for other hardware, are not restored
// and are kept aside. A transient machine keeps nothing.
class BEEBIUM_EXT_TYPE_VISIBLE ExtensionPersistence {
public:
    BEEBIUM_EXT_API virtual ~ExtensionPersistence();

    // The instance's battery-backed units, each named uniquely within it.
    // The pointers must outlive the Extension (typically members of it).
    virtual std::vector<BatteryBackedUnit*> battery_units() = 0;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_EXTENSION_PERSISTENCE_HPP

// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_MACHINES_HPP
#define BEEBIUM_MACHINES_HPP

#include "Machine.hpp"
#include "CpuPolicy.hpp"
#include "ModelBHardware.hpp"
#include "ModelBPlusHardware.hpp"

namespace beebium {

// Convenience type aliases for common machine configurations.
// New configurations can be created by composing policies.

// BBC Model B: NMOS 6502 + Model B hardware (32KB RAM)
using ModelB = Machine<Nmos6502, ModelBHardware>;

// BBC Model B with CMOS 65C02 upgrade
using ModelBCmos = Machine<Cmos65C02, ModelBHardware>;

// BBC Model B+ 64K: NMOS 6502 + Model B+ hardware (64KB RAM with shadow/ANDY)
using ModelBPlus = Machine<Nmos6502, ModelBPlusHardware>;

} // namespace beebium

#endif // BEEBIUM_MACHINES_HPP

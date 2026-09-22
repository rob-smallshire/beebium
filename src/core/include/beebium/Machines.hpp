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
#include "ModelBPlus128KHardware.hpp"
#include "ModelBRomRamBoardHardware.hpp"
#include "ModelBAtplSidewiseHardware.hpp"
#include "ModelBWatfordRomRamHardware.hpp"

namespace beebium {

// Convenience type aliases for common machine configurations.
// New configurations can be created by composing policies.

// BBC Model B: NMOS 6502 + Model B hardware (32KB RAM, 4-socket aliased sideways)
using ModelB = Machine<Nmos6502, ModelBHardware>;

// BBC Model B with ROM/RAM Board: NMOS 6502 + Model B hardware with 16-slot expansion
// Features: 16 independent sideways slots (no aliasing), jsbeeb-style layout
//   Slots 0-7: RAM, Slots 8-12: empty, Slot 13: ADFS, Slot 14: DFS, Slot 15: BASIC
using ModelBRomRamBoard = Machine<Nmos6502, ModelBRomRamBoardHardware>;

// BBC Model B with the ATPL Sidewise expansion board: NMOS 6502 + Model B
// hardware with 16 sideways slots. Slots 0-14 are ROM sockets; slot 15 is the
// board's RAM/ROM socket with write-through and a runtime write-protect switch.
// A specific historical board, intended to replace ModelBRomRamBoard.
using ModelBAtplSidewise = Machine<Nmos6502, ModelBAtplSidewiseHardware>;

// BBC Model B with the Watford Electronics ROM/RAM board: NMOS 6502 + Model B
// hardware with 16 sideways slots. Slots 0-7 are dynamic sideways RAM (32/64/128K
// fitted at launch); slots 8-11 are the motherboard ROM sockets; slots 12,13,15
// are the board's ROM sockets; slot 14 is the board's ROM/static-RAM socket. A
// separate &FF30 write-select latch directs sideways-region writes independently
// of the &FE30 read-select, with S1 (socket-14 read-protect) and S2 (global
// write-protect) switches. A specific historical board.
using ModelBWatfordRomRam = Machine<Nmos6502, ModelBWatfordRomRamHardware>;

// BBC Model B+ 64K: NMOS 6502 + Model B+ hardware (64KB RAM with shadow/ANDY)
using ModelBPlus = Machine<Nmos6502, ModelBPlusHardware>;

// BBC Model B+ 128K: as Model B+ 64K plus four 16 KiB banks of integral
// sideways RAM (AN 030 W/X/Y/Z; W=slot 12, X=slot 13, Y/Z opposite IC71).
using ModelBPlus128K = Machine<Nmos6502, ModelBPlus128KHardware>;

} // namespace beebium

#endif // BEEBIUM_MACHINES_HPP

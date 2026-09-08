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

// Vtable anchors for the coprocessor debugger interfaces. Out-of-line key
// functions give each class a single vtable+typeinfo definition site in this
// shared library, so the server's dynamic_cast across the plugin boundary
// resolves to one typeinfo. Without an anchor, a hidden-visibility weak
// typeinfo would be emitted per module and the cast would fail.

#include <beebium/extension/CoprocessorDebugTarget.hpp>
#include <beebium/extension/Cpu6502DebugTarget.hpp>

namespace beebium {

CoprocessorDebugTarget::~CoprocessorDebugTarget() = default;

Cpu6502DebugTarget::~Cpu6502DebugTarget() = default;

}  // namespace beebium

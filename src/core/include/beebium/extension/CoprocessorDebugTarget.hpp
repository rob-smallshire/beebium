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

#include "Export.hpp"

#include <string_view>

namespace beebium {

// Family-agnostic base for a coprocessor's debugger access, returned by
// CoprocessorExtension::debug_target(). The core, server and service layer
// hold coprocessors only through interfaces that assume nothing about the
// CPU: Z80, 6809, NS32016, 80186 and 80286 coprocessors are anticipated, so a
// concrete debugger surface belongs to a family, not here. cpu_family() names
// the family; the server dynamic_casts to the concrete family interface it can
// serve (today only Cpu6502DebugTarget) and, on failure, simply offers no
// debugger for that coprocessor rather than refusing to run.
//
// Exported (BEEBIUM_EXT_API) with an out-of-line key function so its typeinfo
// is a single symbol across the plugin boundary, which the server's
// dynamic_cast requires.
class BEEBIUM_EXT_API CoprocessorDebugTarget {
public:
    virtual ~CoprocessorDebugTarget();

    // The CPU family this target debugs, e.g. "6502". Stable identifier the
    // server matches against the family interfaces it knows how to serve.
    virtual std::string_view cpu_family() const = 0;
};

}  // namespace beebium

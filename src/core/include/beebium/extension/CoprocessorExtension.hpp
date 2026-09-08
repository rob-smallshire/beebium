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
#include "PeripheralExtension.hpp"

namespace beebium {

class Coprocessor;
class TubeHostBackend;
class CoprocessorDebugTarget;

// A peripheral extension that supplies a Tube coprocessor. It is how the
// server reaches a coprocessor without any concrete coprocessor type: the
// server dynamic_casts a PeripheralExtension to this interface, exactly as it
// already does for EconetTransportExtension. A plugin carries its own copies
// of the Tube bridge and CPU classes, so a cast to a concrete type could never
// succeed across the boundary; a cast to this exported interface does.
//
// init() must leave the socket populated (backend and coprocessor installed)
// and shutdown() must leave it empty, whether the extension installs them
// itself or the server does so from these accessors.
//
// Cross-processor debugger stop needs nothing here: detection lives in the two
// DebuggerControlServiceImpl instances the server owns, so the server wires
// both directions itself, pausing the coprocessor through Coprocessor::pause().
class BEEBIUM_EXT_API CoprocessorExtension : public PeripheralExtension {
public:
    ~CoprocessorExtension() override;

    // The coprocessor to install in the TubeSocket. Valid after init().
    virtual Coprocessor* coprocessor() = 0;

    // The host-facing bridge (Tube ULA or other) to install as the socket
    // backend. Valid after init().
    virtual TubeHostBackend* tube_backend() = 0;

    // Debugger access, or nullptr if this coprocessor offers none. The server
    // matches its family (CoprocessorDebugTarget::cpu_family()) against the
    // debugger interfaces it can serve.
    virtual CoprocessorDebugTarget* debug_target() { return nullptr; }
};

}  // namespace beebium

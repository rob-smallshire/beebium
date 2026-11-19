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

#include "TubeSocket.hpp"

#include <concepts>
#include <cstdint>

namespace beebium {

// Host-side interface for the Tube ULA.
//
// Satisfied by TubeUla (full in-process model). TubeSocket delegates to
// whichever TubeHostBackend implementation is active.
//
// The four required operations are the host processor's view of the Tube:
//   host_read/host_write -- register access at &FEE0-&FEE7
//   hirq                 -- host IRQ output (active when Q=1 and R4 P-to-H has data)
//   reset                -- full hardware reset (HRST)
template<typename T>
concept TubeHostInterface = requires(T& t, const T& ct, uint8_t offset, uint8_t value) {
    { t.host_read(offset) } -> std::convertible_to<uint8_t>;
    { t.host_write(offset, value) } -> std::same_as<void>;
    { ct.hirq() } -> std::convertible_to<bool>;
    { t.reset() } -> std::same_as<void>;
};

// Concept to detect if a hardware type has a Tube socket.
// All current hardware variants (Model B, Model B+, Model B with ROM/RAM board) have this.
template<typename T>
concept HasTubeSocket = requires(T& hw) {
    { hw.tube_socket } -> std::same_as<TubeSocket&>;
};

// The Tube a coprocessor is fitted to: the machine's second Tube (the
// Master's internal one) when that has one fitted, else the Tube. Beebium
// serves one coprocessor at a time, so at most one of them is fitted.
template<typename T>
    requires HasTubeSocket<T>
TubeSocket& fitted_tube_socket(T& hw) {
    if constexpr (requires { hw.internal_tube_socket; }) {
        if (hw.internal_tube_socket.enabled()) return hw.internal_tube_socket;
    }
    return hw.tube_socket;
}
template<typename T>
    requires HasTubeSocket<T>
const TubeSocket& fitted_tube_socket(const T& hw) {
    if constexpr (requires { hw.internal_tube_socket; }) {
        if (hw.internal_tube_socket.enabled()) return hw.internal_tube_socket;
    }
    return hw.tube_socket;
}

// Helper to check if Tube hardware is fitted at runtime.
// Returns true only if the hardware has a TubeSocket AND it is enabled.
template<typename T>
bool tube_present(const T& hw) {
    if constexpr (HasTubeSocket<T>) {
        return fitted_tube_socket(hw).enabled();
    } else {
        return false;
    }
}

}  // namespace beebium

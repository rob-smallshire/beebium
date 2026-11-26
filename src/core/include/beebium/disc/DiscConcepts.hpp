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

#ifndef BEEBIUM_DISC_CONCEPTS_HPP
#define BEEBIUM_DISC_CONCEPTS_HPP

#include "DiscDrive.hpp"
#include "DiscControllerSocket.hpp"
#include "DiscControllerInterface.hpp"

#include <concepts>
#include <memory>

namespace beebium {

// Concept to detect if a hardware type can have disc drives: a controller,
// socketed or built in, brings two (#292). hw.disc_drive(n) is the one way
// to reach them, and is nullptr while a socket is empty.
template<typename T>
concept HasDiscDrives = requires(T& hw) {
    { hw.disc_drive(0) } -> std::convertible_to<DiscDrive*>;
};

// Concept to detect if hardware has a pluggable disc controller socket (Model B)
// as opposed to a built-in controller (Model B+)
template<typename T>
concept HasDiscControllerSocket = requires(T& hw) {
    { hw.disc_socket } -> std::same_as<DiscControllerSocket&>;
    hw.install_disc_controller(std::unique_ptr<DiscControllerInterface>{});
};

// Concept to detect if hardware has optional disc controller (via socket)
// Model B has has_disc_controller() method, Model B+ always has controller
template<typename T>
concept HasOptionalDiscController = requires(const T& hw) {
    { hw.has_disc_controller() } -> std::convertible_to<bool>;
};

// Helper to check if disc controller is present at runtime
// Works for both socketed (Model B) and built-in (Model B+) controllers
template<typename T>
bool disc_controller_present(const T& hw) {
    if constexpr (HasOptionalDiscController<T>) {
        // Hardware has optional controller (Model B with socket)
        return hw.has_disc_controller();
    } else if constexpr (HasDiscDrives<T>) {
        // Hardware has drives but no has_disc_controller() method (Model B+)
        // Assume built-in controller is always present
        return true;
    } else {
        // No disc support
        return false;
    }
}

} // namespace beebium

#endif // BEEBIUM_DISC_CONCEPTS_HPP

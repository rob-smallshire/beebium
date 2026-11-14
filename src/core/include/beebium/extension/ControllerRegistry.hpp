// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_CONTROLLER_REGISTRY_HPP
#define BEEBIUM_EXTENSION_CONTROLLER_REGISTRY_HPP

#include "Controller.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace beebium {

// The controllers attached to one machine, for ControllerService to list
// and address. Non-owning: each controller is owned by its extension, which
// adds itself in init() and removes itself in shutdown().
//
// Every machine has one, so the service always has something to list; with
// no controller attached it is empty. Controllers are added and removed at
// assembly and teardown, while no gRPC thread is serving, and looked up on
// gRPC threads in between, so the registry takes no lock.
class ControllerRegistry {
public:
    ControllerRegistry() = default;
    ControllerRegistry(const ControllerRegistry&) = delete;
    ControllerRegistry& operator=(const ControllerRegistry&) = delete;

    void add(Controller& controller) { controllers_.push_back(&controller); }

    void remove(Controller& controller) {
        controllers_.erase(std::remove(controllers_.begin(), controllers_.end(), &controller),
                           controllers_.end());
    }

    // In the order they were added.
    std::vector<Controller*> controllers() const { return controllers_; }

    // The controller with this id, or null.
    Controller* find(std::string_view controller_id) const {
        for (auto* controller : controllers_) {
            if (controller->controller_id() == controller_id) return controller;
        }
        return nullptr;
    }

private:
    std::vector<Controller*> controllers_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_CONTROLLER_REGISTRY_HPP

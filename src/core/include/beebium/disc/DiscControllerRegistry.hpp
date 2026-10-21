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

#ifndef BEEBIUM_DISC_CONTROLLER_REGISTRY_HPP
#define BEEBIUM_DISC_CONTROLLER_REGISTRY_HPP

#include "DiscControllerInterface.hpp"
#include "Acorn1770DiscController.hpp"
#include "Intel8271DiscController.hpp"

#include <array>
#include <memory>
#include <span>
#include <string_view>

namespace beebium {

// Metadata for a registered disc controller type
// Named ControllerTypeEntry to avoid collision with proto-generated DiscControllerTypeInfo
struct ControllerTypeEntry {
    std::string_view id;           // CLI identifier (e.g., "acorn-1770")
    std::string_view display_name; // Human-readable name (e.g., "Acorn 1770")
    std::string_view fdc_chip;     // FDC chip name (e.g., "WD1770")
    std::string_view description;  // Brief description
};

// Registry of available disc controller types
class DiscControllerRegistry {
public:
    // Get all registered controller types
    static std::span<const ControllerTypeEntry> available() {
        return registry_;
    }

    // Find a controller type by its CLI identifier
    static const ControllerTypeEntry* find(std::string_view id) {
        for (const auto& info : registry_) {
            if (info.id == id) {
                return &info;
            }
        }
        return nullptr;
    }

    // Create a controller instance by CLI identifier
    static std::unique_ptr<DiscControllerInterface> create(std::string_view id) {
        // Hard-coded factory for now - will be extended as more controllers are added
        if (id == "acorn-1770") {
            return std::make_unique<Acorn1770DiscController>();
        }
        if (id == "acorn-8271") {
            return std::make_unique<Intel8271DiscController>();
        }
        // Future controllers:
        // if (id == "opus-ddos") return std::make_unique<OpusDdosDiscController>();
        return nullptr;
    }

    // Check if an identifier is valid
    static bool is_valid(std::string_view id) {
        return find(id) != nullptr;
    }

private:
    // Static registry of available controllers
    // Note: Order matters - first entry is the default recommendation
    static constexpr std::array<ControllerTypeEntry, 2> registry_ = {{
        {
            .id = "acorn-1770",
            .display_name = "Acorn 1770",
            .fdc_chip = "WD1770",
            .description = "Standard Acorn disc controller upgrade for BBC Model B"
        },
        {
            .id = "acorn-8271",
            .display_name = "Acorn 8271",
            .fdc_chip = "8271",
            .description = "Original Intel 8271 disc interface for BBC Model B (DFS 0.90-1.22)"
        }
        // Future controllers will be added here
    }};
};

} // namespace beebium

#endif // BEEBIUM_DISC_CONTROLLER_REGISTRY_HPP

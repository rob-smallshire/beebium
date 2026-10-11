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

#ifndef BEEBIUM_EXTENSION_REGISTRY_HPP
#define BEEBIUM_EXTENSION_REGISTRY_HPP

#include "ExtensionContext.hpp"
#include "PeripheralExtension.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace beebium {

// Manages extension lifecycle: registration, dependency resolution,
// initialisation in topological order, and reverse-order shutdown.
class ExtensionRegistry {
public:
    // Register a built-in extension point name (e.g. "1mhz-bus").
    // These are always considered satisfied during dependency resolution.
    void register_extension_point(std::string_view name) {
        extension_points_.emplace(name);
    }

    // Register an extension for later initialisation.
    //
    // If the extension has no explicit `id` in its config (the usual
    // case for any extension the user didn't tag with a CLI/preset
    // `id=`), fill one in via make_extension_id() so that every
    // registered extension is addressable by a unique, stable id.
    // First instance of a type gets `id == manifest.name`; subsequent
    // get the `name-N` suffix scheme.
    void register_extension(std::unique_ptr<PeripheralExtension> ext) {
        if (ext->id().empty()) {
            std::vector<std::string> existing;
            existing.reserve(extensions_.size());
            for (const auto& e : extensions_) {
                existing.emplace_back(e->id());
            }
            ext->set_config_value(
                "id", make_extension_id(ext->name(), existing));
        }
        extensions_.push_back(std::move(ext));
    }

    // Resolve dependencies via topological sort and init extensions in order.
    // Throws std::runtime_error on unsatisfied dependencies or cycles.
    void resolve_and_init(ExtensionContext& ctx) {
        std::set<std::string, std::less<>> available(extension_points_);
        std::vector<PeripheralExtension*> pending;

        for (auto& ext : extensions_) {
            pending.push_back(ext.get());
        }

        // Kahn's algorithm: repeatedly init extensions whose dependencies
        // are all satisfied.
        while (!pending.empty()) {
            auto it = std::find_if(pending.begin(), pending.end(),
                [&available](PeripheralExtension* ext) {
                    for (auto dep : ext->attaches_to()) {
                        if (!available.contains(std::string(dep))) {
                            return false;
                        }
                    }
                    return true;
                });

            if (it == pending.end()) {
                std::string names;
                for (auto* ext : pending) {
                    if (!names.empty()) names += ", ";
                    names += ext->name();
                }
                throw std::runtime_error(
                    "ExtensionRegistry: unsatisfied dependencies or cycle "
                    "involving: " + names);
            }

            auto* ext = *it;
            pending.erase(it);

            ext->init(ctx);
            init_order_.push_back(ext);

            for (auto provided : ext->provides()) {
                available.emplace(provided);
                // Make this extension available to downstream extensions
                // that attach_to this extension point.
                ctx.register_provider(provided, ext);
            }
        }
    }

    // Shutdown all extensions in reverse init order.
    void shutdown() {
        for (auto it = init_order_.rbegin(); it != init_order_.rend(); ++it) {
            (*it)->shutdown();
        }
        init_order_.clear();
    }

    // Iterate initialised extensions (for PeripheralExtensionService discovery).
    std::span<PeripheralExtension* const> extensions() const {
        return init_order_;
    }

    // The machine's own extension points, as registered.
    const std::set<std::string, std::less<>>& extension_points() const {
        return extension_points_;
    }

    // Query the number of registered extensions.
    size_t extension_count() const { return extensions_.size(); }

    // Query the number of initialised extensions.
    size_t init_count() const { return init_order_.size(); }

private:
    std::set<std::string, std::less<>> extension_points_;
    std::vector<std::unique_ptr<PeripheralExtension>> extensions_;
    std::vector<PeripheralExtension*> init_order_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_REGISTRY_HPP

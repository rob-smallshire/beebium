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

#ifndef BEEBIUM_EXTENSION_ECONET_TRANSPORT_REGISTRY_HPP
#define BEEBIUM_EXTENSION_ECONET_TRANSPORT_REGISTRY_HPP

#include "Export.hpp"
#include "EconetTransportExtension.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace grpc { class Service; }

namespace beebium {

// Ordered container of zero or more EconetTransportExtension instances.
//
// No singleton constraint: per-machine cardinality (e.g. "exactly one
// for BBC Micro / Master / Master Compact") is enforced at machine-setup
// time by the caller, not here. A future Acorn-Econet-Bridge machine
// type with two memory-mapped ADLCs will hold two transports here.
class BEEBIUM_EXT_TYPE_VISIBLE EconetTransportRegistry {
public:
    EconetTransportRegistry() = default;
    // See ExtensionUi.hpp for the BEEBIUM_EXT_TYPE_VISIBLE / per-method
    // BEEBIUM_EXT_API split.
    BEEBIUM_EXT_API ~EconetTransportRegistry();
    EconetTransportRegistry(const EconetTransportRegistry&) = delete;
    EconetTransportRegistry& operator=(const EconetTransportRegistry&) = delete;

    // Take ownership of a transport extension and append it to the
    // registry. Order of insertion is preserved.
    //
    // The extension must have an `id`, by which it is addressed (e.g. for
    // ExtensionUiService subscriptions and ExtensionRpc routing): the server
    // names every instance once its configuration is resolved
    // (assign_extension_ids), so the registry never names one itself. Throws
    // std::invalid_argument for an extension with none.
    void add(std::unique_ptr<EconetTransportExtension> extension) {
        if (extension->id().empty()) {
            throw std::invalid_argument(
                "EconetTransportRegistry: transport '" + std::string(extension->name()) +
                "' has no id");
        }
        extensions_.push_back(std::move(extension));
    }

    // Number of transport extensions currently held.
    std::size_t size() const { return extensions_.size(); }
    bool empty() const { return extensions_.empty(); }

    // Read-only access to the transports in insertion order.
    const std::vector<std::unique_ptr<EconetTransportExtension>>& extensions() const {
        return extensions_;
    }

    // Mutable access for the machine-setup site that needs to call
    // create_backend()/on_station_id_changed() on each extension.
    std::vector<std::unique_ptr<EconetTransportExtension>>& extensions() {
        return extensions_;
    }

private:
    std::vector<std::unique_ptr<EconetTransportExtension>> extensions_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_ECONET_TRANSPORT_REGISTRY_HPP

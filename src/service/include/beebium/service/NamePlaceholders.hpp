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

// The core's machine-name placeholders (#153): the machine's own, and the
// Econet socket's. Extensions add theirs through Extension::name_placeholders.

#ifndef BEEBIUM_SERVICE_NAME_PLACEHOLDERS_HPP
#define BEEBIUM_SERVICE_NAME_PLACEHOLDERS_HPP

#include "beebium/econet/AunBackend.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/NamePlaceholderProvider.hpp"

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::service {

// The core's placeholder descriptions, for listing before a machine exists
// (the list-name-placeholders subcommand) as well as by the providers below.
inline std::vector<NamePlaceholderInfo> machine_name_placeholder_infos() {
    return {
        {"machine-model", "Machine model",
         "The machine's model, as the server reports it (\"BBC Model B\"). "
         "Never changes.",
         "Machine"},
        {"machine-preset", "Preset",
         "The name of the preset the machine was launched from; not "
         "applicable when it was launched without one. Never changes.",
         "Machine"},
    };
}

inline std::vector<NamePlaceholderInfo> econet_name_placeholder_infos() {
    return {
        {"econet-station", "Econet station",
         "The station number in force: the number the machine's filing "
         "system read from the station links at its last Break. A "
         "renumber shows after the next Break.",
         "Econet"},
        {"econet-net", "Econet net",
         "This machine's Econet net number (0 for the local net). Set at "
         "launch.",
         "Econet"},
        {"econet-transport", "Econet transport",
         "The transport carrying Econet (\"AUN\", \"Piconet\"), as its "
         "sidebar labels it; empty when Econet is fitted with no "
         "transport. Changes when Econet is enabled or disabled.",
         "Econet"},
    };
}

// The `machine` domain: facts about the machine that differ between machines
// sharing a hand-written template.
class MachineNamePlaceholders final : public NamePlaceholderProvider {
public:
    // `model_name` is the display name SystemInfo reports.
    explicit MachineNamePlaceholders(std::string model_name)
        : model_name_(std::move(model_name)) {}

    // The name of the preset the machine was launched from, or empty. Set
    // before the server starts; read-only afterwards.
    void set_preset_name(std::string preset_name) { preset_name_ = std::move(preset_name); }

    std::vector<std::string> placeholder_domains() const override { return {"machine"}; }

    std::vector<NamePlaceholderInfo> placeholders() const override {
        return machine_name_placeholder_infos();
    }

    NamePlaceholderValue placeholder_value(std::string_view key) const override {
        if (key == "machine-model") return {model_name_, true};
        if (key == "machine-preset") return {preset_name_, !preset_name_.empty()};
        return {"", false};
    }

private:
    const std::string model_name_;
    std::string preset_name_;
};

// The `econet` domain: the Econet socket and its transport. Every value is
// read lock-free or through a co-owning handle, so it is safe from a server
// thread while the machine runs. Not applicable while no Econet is fitted.
template <typename MachineType>
class EconetNamePlaceholders final : public NamePlaceholderProvider {
public:
    explicit EconetNamePlaceholders(MachineType& machine) : machine_(machine) {}

    // The machine's transport registry (holding at most one transport), or
    // nullptr when there is none. Fixed after launch.
    void set_transport_registry(const EconetTransportRegistry* registry) {
        transport_registry_.store(registry, std::memory_order_release);
    }

    std::vector<std::string> placeholder_domains() const override { return {"econet"}; }

    std::vector<NamePlaceholderInfo> placeholders() const override {
        return econet_name_placeholder_infos();
    }

    NamePlaceholderValue placeholder_value(std::string_view key) const override {
        using Memory = typename MachineType::Memory;
        if constexpr (HasEconetSocket<Memory>) {
            const auto& socket = machine_.state().memory.econet_socket;
            const auto station = socket.station_in_force();
            if (!station) {
                return {"", false};
            }
            if (key == "econet-station") {
                return {std::to_string(*station), true};
            }
            if (key == "econet-net") {
                // Co-owning handle: a concurrent DisableEconet cannot free the
                // backend under this read.
                const auto backend = socket.backend_shared();
                int net = 0;
                if (const auto* aun = dynamic_cast<const AunBackend*>(backend.get())) {
                    net = aun->local_net();
                }
                return {std::to_string(net), true};
            }
            if (key == "econet-transport") {
                const auto* registry = transport_registry_.load(std::memory_order_acquire);
                if (registry == nullptr || registry->empty()) {
                    return {"", true};
                }
                return {registry->extensions().front()->label(), true};
            }
        }
        return {"", false};
    }

private:
    MachineType& machine_;
    std::atomic<const EconetTransportRegistry*> transport_registry_{nullptr};
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_NAME_PLACEHOLDERS_HPP

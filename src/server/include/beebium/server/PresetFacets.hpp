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

// A preset's facets: what a catalogue filters presets by (the model family,
// the coprocessor, the disc controller, the network, the storage and the
// peripherals), derived by the server from the loaded preset, the model's
// entry in MachineModels and the extensions' manifests, so a client filters
// without reading preset content. list-presets gives each row's facets and
// describe-facets the facet list (docs/preset-schema.md, "Facets").

#pragma once

#include "beebium/MachineModels.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/extension/ExtensionResolver.hpp"
#include "beebium/server/PresetLoader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

struct PresetFacet {
    enum class Kind { Exclusive, Set };

    std::string_view key;
    std::string_view label;
    Kind kind;

    std::string_view kind_name() const { return kind == Kind::Exclusive ? "exclusive" : "set"; }
};

// The facets in display order. An exclusive facet gives every preset exactly
// one value; a set facet gives it any number, none included.
inline constexpr std::array<PresetFacet, 6> kPresetFacets{{
    {"model", "Model", PresetFacet::Kind::Exclusive},
    {"coprocessor", "Coprocessor", PresetFacet::Kind::Exclusive},
    {"disc_controller", "Disc controller", PresetFacet::Kind::Exclusive},
    {"network", "Network", PresetFacet::Kind::Exclusive},
    {"storage", "Storage", PresetFacet::Kind::Set},
    {"peripherals", "Peripherals", PresetFacet::Kind::Set},
}};

// The value of an exclusive facet a preset has nothing for.
inline constexpr std::string_view kNoFacetValue = "None";

namespace detail {

// Whether an extension is a coprocessor: it attaches to a Tube, the external
// one or the Master's internal one.
inline bool is_coprocessor(const beebium::ExtensionManifest& manifest) {
    const auto points = manifest.all_points();
    return std::any_of(points.begin(), points.end(), [](const std::string& point) {
        return point == "tube" || point == "internal-tube";
    });
}

inline void add_once(nlohmann::ordered_json& values, const std::string& value) {
    for (const auto& existing : values) {
        if (existing == value) return;
    }
    values.push_back(value);
}

inline std::string disc_controller_facet(const PresetConfig& preset,
                                         const beebium::MachineModel& model) {
    if (!model.builtin_fdc_chip.empty()) {
        const auto* builtin = beebium::DiscControllerRegistry::find_by_chip(model.builtin_fdc_chip);
        return std::string(builtin ? builtin->short_name : model.builtin_fdc_chip);
    }
    if (preset.storage && preset.storage->fdc_socket_id) {
        if (const auto* fitted = beebium::DiscControllerRegistry::find(*preset.storage->fdc_socket_id)) {
            return std::string(fitted->short_name);
        }
    }
    return std::string(kNoFacetValue);
}

}  // namespace detail

// The facets of `preset` on `model`, keyed by facet key in kPresetFacets'
// order: a string for an exclusive facet, an array of strings for a set one.
//
// - model: the model's family.
// - coprocessor: the facet name of the first extension that attaches to a
//   Tube, else "None".
// - disc_controller: the model's built-in controller, else the controller in
//   the preset's FDC socket, by its short name ("1770", "8271"), else "None".
// - network: the facet name of the preset's Econet transport, else "None".
// - storage: "Floppy" when there is a disc controller, then the storage
//   medium of each extension whose manifest names one ("Hard disc"), then
//   "Cassette" when the model has a cassette interface.
// - peripherals: the facet name of every other extension, in preset order.
//
// An extension the resolver does not know (a plugin missing from this
// install) is listed among the peripherals by its name.
inline nlohmann::ordered_json preset_facets(const PresetConfig& preset,
                                            const beebium::MachineModel& model,
                                            const beebium::ExtensionResolver& resolver) {
    using ojson = nlohmann::ordered_json;

    std::string coprocessor(kNoFacetValue);
    bool has_coprocessor = false;
    ojson extension_storage = ojson::array();
    ojson peripherals = ojson::array();
    for (const auto& extension : preset.extensions) {
        const auto* manifest = resolver.find_by_name(extension.name);
        if (!manifest) {
            detail::add_once(peripherals, extension.name);
        } else if (detail::is_coprocessor(*manifest)) {
            if (!has_coprocessor) coprocessor = manifest->facet_name();
            has_coprocessor = true;
        } else if (!manifest->storage.empty()) {
            detail::add_once(extension_storage, manifest->storage);
        } else if (manifest->extension_kind != "econet-transport") {
            detail::add_once(peripherals, manifest->facet_name());
        }
    }

    std::string network(kNoFacetValue);
    if (preset.econet && preset.econet->transport) {
        const auto& name = preset.econet->transport->name;
        const auto* manifest = resolver.find_by_name(name);
        network = manifest ? manifest->facet_name() : name;
    }

    const std::string disc_controller = detail::disc_controller_facet(preset, model);

    ojson storage = ojson::array();
    if (disc_controller != kNoFacetValue) storage.push_back("Floppy");
    for (const auto& medium : extension_storage) detail::add_once(storage, medium.get<std::string>());
    if (model.has_cassette) storage.push_back("Cassette");

    ojson facets;
    facets["model"] = std::string(model.family);
    facets["coprocessor"] = coprocessor;
    facets["disc_controller"] = disc_controller;
    facets["network"] = network;
    facets["storage"] = storage;
    facets["peripherals"] = peripherals;
    return facets;
}

}  // namespace beebium::server

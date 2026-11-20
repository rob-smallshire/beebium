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

// Every machine model Beebium emulates, by the id a preset's "model" names
// and the server executable is called after (beebium-<id>), with the name a
// user sees. The one list: each hardware policy takes its MACHINE_TYPE and
// MACHINE_DISPLAY_NAME from its entry here, and a command that handles any
// model's presets (list-presets --all-models, import-preset) consults it.
//
// Each entry also carries the facts about a model that a command handling
// another model's presets needs without that model's hardware policy: its
// family (the name a catalogue filters by), the disc controller it has built
// in, whether it has a cassette interface, and the words a preset search
// knows it by. describe-preset-schema reads the same entry, and checks it
// against the policy.

#pragma once

#include <array>
#include <string_view>

namespace beebium {

struct MachineModel {
    std::string_view id;
    std::string_view display_name;
    // The family a model belongs to: "Model B" for the Model B and every
    // board fitted to it, "Model B+" for both B+ models, "Master 128".
    std::string_view family;
    // The disc controller chip built into the machine ("WD1770"), or empty
    // when it has none (the Model B family, whose controller is socketed).
    std::string_view builtin_fdc_chip = {};
    bool has_cassette = true;
    // The words a preset search finds the model by that its names do not
    // spell, a list-presets row's keywords; empty entries are unused.
    std::array<std::string_view, 6> keywords = {};
};

namespace machine_models {

inline constexpr MachineModel kModelB{
    "model-b", "BBC Model B", "Model B", {}, true, {"BBC", "Micro", "Acorn"}};
inline constexpr MachineModel kModelBPlus{"model-b-plus", "BBC Model B+ 64K", "Model B+", "WD1770",
                                          true, {"BBC", "Micro", "Acorn", "B+", "Plus"}};
inline constexpr MachineModel kModelBPlus128K{"model-b-plus-128k", "BBC Model B+ 128K", "Model B+",
                                              "WD1770", true,
                                              {"BBC", "Micro", "Acorn", "B+", "Plus"}};
inline constexpr MachineModel kModelBRomRam{"model-b-romram", "BBC Model B with ROM/RAM Board",
                                            "Model B", {}, true,
                                            {"BBC", "Micro", "Acorn", "ROM/RAM board"}};
inline constexpr MachineModel kModelBAtplSidewise{"model-b-atpl-sidewise",
                                                  "BBC Model B with ATPL Sidewise", "Model B", {},
                                                  true,
                                                  {"BBC", "Micro", "Acorn", "ATPL", "Sidewise"}};
inline constexpr MachineModel kModelBWatfordRomRam{
    "model-b-watford-rom-ram", "BBC Model B with Watford ROM/RAM board", "Model B", {}, true,
    {"BBC", "Micro", "Acorn", "Watford", "ROM/RAM board"}};
inline constexpr MachineModel kModelBIntegraB{"model-b-integra-b", "BBC Model B with Integra-B",
                                              "Model B", {}, true,
                                              {"BBC", "Micro", "Acorn", "Integra-B", "Computech"}};
inline constexpr MachineModel kMaster128{"master-128", "BBC Master 128", "Master 128", "WD1770",
                                         true, {"BBC", "Micro", "Acorn", "Master"}};

inline constexpr std::array kAll{kModelB,          kModelBPlus,          kModelBPlus128K,
                                 kModelBRomRam,    kModelBAtplSidewise,  kModelBWatfordRomRam,
                                 kModelBIntegraB,  kMaster128};

// The model with `id`, or nullptr when there is none.
constexpr const MachineModel* find(std::string_view id) {
    for (const auto& model : kAll) {
        if (model.id == id) return &model;
    }
    return nullptr;
}

}  // namespace machine_models
}  // namespace beebium

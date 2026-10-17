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

#pragma once

#include <array>
#include <string_view>

namespace beebium {

struct MachineModel {
    std::string_view id;
    std::string_view display_name;
};

namespace machine_models {

inline constexpr MachineModel kModelB{"model-b", "BBC Model B"};
inline constexpr MachineModel kModelBPlus{"model-b-plus", "BBC Model B+ 64K"};
inline constexpr MachineModel kModelBPlus128K{"model-b-plus-128k", "BBC Model B+ 128K"};
inline constexpr MachineModel kModelBRomRam{"model-b-romram", "BBC Model B with ROM/RAM Board"};
inline constexpr MachineModel kModelBAtplSidewise{"model-b-atpl-sidewise",
                                                  "BBC Model B with ATPL Sidewise"};
inline constexpr MachineModel kModelBWatfordRomRam{"model-b-watford-rom-ram",
                                                   "BBC Model B with Watford ROM/RAM board"};
inline constexpr MachineModel kModelBIntegraB{"model-b-integra-b", "BBC Model B with Integra-B"};

inline constexpr std::array kAll{kModelB,          kModelBPlus,          kModelBPlus128K,
                                 kModelBRomRam,    kModelBAtplSidewise,  kModelBWatfordRomRam,
                                 kModelBIntegraB};

// The model with `id`, or nullptr when there is none.
constexpr const MachineModel* find(std::string_view id) {
    for (const auto& model : kAll) {
        if (model.id == id) return &model;
    }
    return nullptr;
}

}  // namespace machine_models
}  // namespace beebium

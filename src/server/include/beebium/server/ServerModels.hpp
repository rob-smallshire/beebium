// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// The machine models the one server executable runs, each by its entry point
// in a translation unit of its own (main_<model>.cpp), in MachineModels'
// order and by its ids, which name the model's executable (beebium-<id>).

#pragma once

#include "beebium/MachineModels.hpp"
#include "beebium/server/MultiCall.hpp"

#include <array>

namespace beebium::server {

int run_model_b(int argc, char* argv[]);
int run_model_b_plus(int argc, char* argv[]);
int run_model_b_plus_128k(int argc, char* argv[]);
int run_model_b_romram(int argc, char* argv[]);
int run_model_b_atpl_sidewise(int argc, char* argv[]);
int run_model_b_watford_rom_ram(int argc, char* argv[]);
int run_model_b_integra_b(int argc, char* argv[]);
int run_master_128(int argc, char* argv[]);

namespace models = beebium::machine_models;

inline constexpr std::array<ServerModel, 8> kServerModels{{
    {models::kModelB.id, run_model_b},
    {models::kModelBPlus.id, run_model_b_plus},
    {models::kModelBPlus128K.id, run_model_b_plus_128k},
    {models::kModelBRomRam.id, run_model_b_romram},
    {models::kModelBAtplSidewise.id, run_model_b_atpl_sidewise},
    {models::kModelBWatfordRomRam.id, run_model_b_watford_rom_ram},
    {models::kModelBIntegraB.id, run_model_b_integra_b},
    {models::kMaster128.id, run_master_128},
}};

constexpr bool server_models_follow_machine_models() {
    if (kServerModels.size() != models::kAll.size()) return false;
    for (size_t i = 0; i < kServerModels.size(); ++i) {
        if (kServerModels[i].id != models::kAll[i].id) return false;
    }
    return true;
}
static_assert(server_models_follow_machine_models(),
              "every model in MachineModels has an entry point, in the same order");

}  // namespace beebium::server

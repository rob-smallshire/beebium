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

// The model-b-plus model's entry point in the one server executable
// (main.cpp): a translation unit of its own, so the eight machines compile in
// parallel.

#include "beebium/server/ServerMain.hpp"
#include "beebium/server/ServerModels.hpp"
#include "beebium/Machines.hpp"

namespace beebium::server {

int run_model_b_plus(int argc, char* argv[]) {
    return server_main<beebium::ModelBPlus>(argc, argv);
}

}  // namespace beebium::server

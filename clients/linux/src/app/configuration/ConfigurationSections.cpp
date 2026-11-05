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

#include "app/NewMachineDialog.hpp"
#include "app/configuration/MemorySection.hpp"
#include "app/configuration/PeripheralsSection.hpp"
#include "app/configuration/StorageSection.hpp"

namespace beebium::linux_client {

// The configuration editor's sections, each shown only where the model's
// schema has what it edits.
std::vector<std::function<ConfigurationSection*(NewMachineDialog&)>>
configuration_section_makers() {
    return {
        [](NewMachineDialog& dialog) -> ConfigurationSection* {
            const auto& schema = dialog.schema();
            return schema && schema->section("storage") ? new StorageSection(dialog) : nullptr;
        },
        [](NewMachineDialog& dialog) -> ConfigurationSection* {
            const auto& schema = dialog.schema();
            return schema && schema->section("sideways_bank") ? new MemorySection(dialog) : nullptr;
        },
        [](NewMachineDialog& dialog) -> ConfigurationSection* {
            const auto& schema = dialog.schema();
            return schema && PeripheralsSection::applies(*schema) ? new PeripheralsSection(dialog)
                                                                  : nullptr;
        },
    };
}

}  // namespace beebium::linux_client

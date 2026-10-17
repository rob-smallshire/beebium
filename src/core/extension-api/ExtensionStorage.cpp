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

// Vtable anchor for ExtensionStorage. Same rationale as ExtensionUi.cpp:
// the class is declared with BEEBIUM_EXT_API, so MSVC needs the vtable
// to live in the exporting DLL via at least one non-inline virtual
// definition. The out-of-line destructor provides that anchor.

#include "beebium/extension/ExtensionStorage.hpp"

namespace beebium {

ExtensionStorage::~ExtensionStorage() = default;

bool ExtensionStorage::reopen(std::string_view device_id, const std::filesystem::path&,
                              std::string& error) {
    error = "storage device '" + std::string(device_id) + "' cannot be moved to another image";
    return false;
}

}  // namespace beebium

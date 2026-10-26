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

#include "HybridMusic5000.hpp"
#include <beebium/extension/Extension.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

extern "C" {

BEEBIUM_PLUGIN_EXPORT
beebium::Extension* beebium_create_extension(const beebium::ExtensionManifest& manifest) {
    auto* ext = new beebium::HybridMusic5000();
    ext->set_manifest(manifest);
    return ext;
}

}

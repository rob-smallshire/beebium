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

// Plugin entry point for the Acorn 65C02 second processor. The server loads
// this shared object from <exe-dir>/extensions/acorn-65c02-coprocessor/,
// reads the sibling manifest.json, and reaches the coprocessor only through
// the abstract CoprocessorExtension interface.

#include "SecondProcessor65C02Extension.hpp"

#include <beebium/extension/Extension.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

extern "C" {
BEEBIUM_PLUGIN_EXPORT
beebium::Extension* beebium_create_extension(const beebium::ExtensionManifest& manifest) {
    auto* ext = new beebium::SecondProcessor65C02Extension();
    ext->set_manifest(manifest);
    return ext;
}
}

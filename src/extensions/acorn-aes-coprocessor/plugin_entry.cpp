// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// plugin_entry.cpp, the function Beebium's loader calls
// version 2.0.0, 20 November 2026.

// The function Beebium's loader looks for when it loads the plugin: it makes the extension and
// hands it the manifest. At the top of the folder, as in Beebium's own coprocessors.

#include "beebium/AesExtension.hpp"

#include <beebium/extension/Extension.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

extern "C" {
// Makes the extension and hands it the plugin's manifest; Beebium owns what it returns.
BEEBIUM_PLUGIN_EXPORT
beebium::Extension* beebium_create_extension(const beebium::ExtensionManifest& manifest) {
    auto* extension = new AesExtension();
    extension->set_manifest(manifest);
    return extension;
}
}

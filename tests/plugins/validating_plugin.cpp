// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// A stub plugin exporting the configuration check, so test_plugin_loader can
// check that PluginLoader calls it: speed=fast is an error and any mode a
// warning. Its create function must never be reached.

#include <beebium/extension/ConfigValidation.hpp>
#include <beebium/extension/ExtensionAbi.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

#include <cstdint>
#include <cstdlib>

extern "C" {

BEEBIUM_PLUGIN_EXPORT
std::uint32_t beebium_plugin_abi_version() {
    return beebium::kExtensionAbiVersion;
}

BEEBIUM_PLUGIN_EXPORT
beebium::Extension* beebium_create_extension(const beebium::ExtensionManifest&) {
    std::abort();
}

BEEBIUM_PLUGIN_EXPORT
void beebium_validate_extension_config(
        const beebium::ExtensionManifest& manifest,
        const std::map<std::string, std::string>& config,
        const std::map<std::string, std::vector<std::string>>&,
        std::vector<beebium::ConfigDiagnostic>& diagnostics) {
    if (auto it = config.find("speed"); it != config.end() && it->second == "fast") {
        diagnostics.push_back({"speed", manifest.name + " cannot go fast."});
    }
    if (auto it = config.find("mode"); it != config.end()) {
        diagnostics.push_back({"mode", "Mode " + it->second + " is unusual.",
                               beebium::ConfigDiagnostic::Severity::Warning});
    }
}

}

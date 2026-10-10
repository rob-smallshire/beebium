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

#include "AcornRtcConfig.hpp"
#include "AcornRtcExtension.hpp"
#include "TimeParser.hpp"
#include <beebium/extension/ConfigValidation.hpp>
#include <beebium/extension/Extension.hpp>
#include <beebium/extension/ExtensionManifest.hpp>

extern "C" {

BEEBIUM_PLUGIN_EXPORT
beebium::Extension* beebium_create_extension(const beebium::ExtensionManifest& manifest) {
    auto* ext = new beebium::AcornRtcExtension();
    ext->set_manifest(manifest);
    return ext;
}

// The configuration check (beebium::ValidateConfigFn): a start the register
// layout cannot hold is refused before launch, judged by the host clock now.
BEEBIUM_PLUGIN_EXPORT
void beebium_validate_extension_config(
        const beebium::ExtensionManifest& manifest,
        const std::map<std::string, std::string>& config,
        const std::map<std::string, std::vector<std::string>>& /*list_config*/,
        std::vector<beebium::ConfigDiagnostic>& diagnostics) {
    for (auto& d : beebium::check_acorn_rtc_config(manifest, config,
                                                   beebium::current_local_datetime())) {
        diagnostics.push_back(std::move(d));
    }
}

}

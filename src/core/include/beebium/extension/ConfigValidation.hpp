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

#ifndef BEEBIUM_EXTENSION_CONFIG_VALIDATION_HPP
#define BEEBIUM_EXTENSION_CONFIG_VALIDATION_HPP

#include "ExtensionManifest.hpp"

#include <map>
#include <string>
#include <vector>

namespace beebium {

// A problem an extension finds with one of its instance configurations,
// before any machine is built: what the manifest's schema cannot say, such as
// a value that is valid alone but not with another parameter's. The framework
// locates it at the parameter in the preset (or names the CLI flag), merges it
// into resolve-preset's diagnostics, and refuses to start on an error.
struct ConfigDiagnostic {
    enum class Severity { Error, Warning };

    std::string parameter;  // the parameter key it is about; "" for the instance
    std::string message;    // a sentence a user understands
    Severity severity = Severity::Error;
};

// The optional configuration check a plugin exports, by this name and
// signature, to validate an instance's config. It receives the config already
// normalised against the manifest (defaults filled in, choices and types
// checked, framework keys id and label included) and appends what it finds to
// `diagnostics`; it must not throw. A plugin without it validates nothing
// beyond its manifest.
using ValidateConfigFn = void (*)(const ExtensionManifest& manifest,
                                  const std::map<std::string, std::string>& config,
                                  const std::map<std::string, std::vector<std::string>>& list_config,
                                  std::vector<ConfigDiagnostic>& diagnostics);

inline constexpr const char* kValidateConfigSymbol = "beebium_validate_extension_config";

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_CONFIG_VALIDATION_HPP

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

#ifndef BEEBIUM_LINUX_PRESET_PRESET_RESOLUTION_HPP
#define BEEBIUM_LINUX_PRESET_PRESET_RESOLUTION_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::linux_client {

// What the server makes of a preset (client specification 5.2; docs/
// preset-schema.md, resolve-preset): the machine it describes, every key
// present, and every problem with it. The server decides validity; the
// client only shows what it says.

struct PresetDiagnostic {
    enum class Severity { error, warning };
    Severity severity = Severity::error;
    std::string path;      // a JSON pointer to the key at fault; "" for the whole file
    std::string location;  // where that is, in the schema's own words
    std::string message;

    bool error() const { return severity == Severity::error; }
};

struct PresetResolution {
    // Null when the preset could not be loaded at all.
    std::optional<nlohmann::ordered_json> preset;
    std::vector<PresetDiagnostic> diagnostics;

    // Errors block a launch; warnings do not.
    bool blocked() const;
};

// resolve-preset --json's standard output, which it writes whatever its exit
// status; nothing when it does not parse as one.
std::optional<PresetResolution> parse_resolution(std::string_view output);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_PRESET_RESOLUTION_HPP

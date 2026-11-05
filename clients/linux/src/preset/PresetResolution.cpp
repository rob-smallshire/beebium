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

#include "preset/PresetResolution.hpp"

#include <algorithm>

namespace beebium::linux_client {

using nlohmann::ordered_json;

bool PresetResolution::blocked() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const PresetDiagnostic& d) { return d.error(); });
}

std::optional<PresetResolution> parse_resolution(std::string_view output) {
    ordered_json json;
    try {
        json = ordered_json::parse(output);
    } catch (const nlohmann::json::parse_error&) {
        return std::nullopt;
    }
    if (!json.is_object()) {
        return std::nullopt;
    }
    PresetResolution resolution;
    if (const auto preset = json.find("preset"); preset != json.end() && preset->is_object()) {
        resolution.preset.emplace(*preset);
    }
    if (const auto found = json.find("diagnostics");
        found != json.end() && found->is_array()) {
        for (const auto& entry : *found) {
            if (!entry.is_object()) {
                continue;
            }
            PresetDiagnostic diagnostic;
            diagnostic.severity = entry.value("severity", std::string("error")) == "warning"
                                      ? PresetDiagnostic::Severity::warning
                                      : PresetDiagnostic::Severity::error;
            diagnostic.path = entry.value("path", std::string());
            diagnostic.location = entry.value("location", std::string());
            diagnostic.message = entry.value("message", std::string());
            resolution.diagnostics.push_back(std::move(diagnostic));
        }
    }
    return resolution;
}

}  // namespace beebium::linux_client

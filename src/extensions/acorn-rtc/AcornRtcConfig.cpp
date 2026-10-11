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
#include "TimeParser.hpp"

#include <stdexcept>

namespace beebium {

namespace {

const std::string* find_value(const std::map<std::string, std::string>& config,
                              const std::string& key) {
    auto it = config.find(key);
    return it != config.end() && !it->second.empty() ? &it->second : nullptr;
}

// "the Acorn original (4-bit year in R1) register layout holds 1981-1996"
std::string layout_holds(const ExtensionManifest& manifest, const std::string& layout_name,
                         RegisterLayout layout) {
    std::string label = layout_name;
    for (const auto& p : manifest.parameters) {
        if (p.key != "layout") continue;
        for (const auto& c : p.choices) {
            if (c.value == layout_name) label = c.label;
        }
    }
    const auto [first, last] = Saf3019p::year_range(layout);
    return "the " + label + " register layout holds " + std::to_string(first) + "-" +
           std::to_string(last);
}

bool holds(RegisterLayout layout, int year) {
    const auto [first, last] = Saf3019p::year_range(layout);
    return year >= first && year <= last;
}

}  // namespace

RegisterLayout acorn_rtc_layout(const std::map<std::string, std::string>& config) {
    const auto* name = find_value(config, "layout");
    if (!name) throw std::invalid_argument("no layout given");
    const std::string& layout_name = *name;
    const auto layout = register_layout_from_name(layout_name);
    if (!layout) {
        throw std::invalid_argument(
            "unknown layout '" + layout_name +
            "' (valid: '4bit-year-in-r1', '7bit-year-in-r7', '7bit-year-in-r1r5')");
    }
    return *layout;
}

Saf3019p::DateTime acorn_rtc_start_time(const std::map<std::string, std::string>& config,
                                        const Saf3019p::DateTime& now) {
    if (const auto* time = find_value(config, "time")) return parse_absolute_time(*time);
    if (const auto* offset = find_value(config, "offset")) {
        return apply_relative_offset(*offset, now);
    }
    return now;
}

std::vector<ConfigDiagnostic> check_acorn_rtc_config(
        const ExtensionManifest& manifest, const std::map<std::string, std::string>& config,
        const Saf3019p::DateTime& now) {
    std::vector<ConfigDiagnostic> found;
    RegisterLayout layout;
    try {
        layout = acorn_rtc_layout(config);
    } catch (const std::invalid_argument& e) {
        found.push_back({"layout", e.what()});
        return found;
    }
    // acorn_rtc_layout has refused a config naming no layout.
    const std::string& layout_name = *find_value(config, "layout");

    // The parameter the start comes from: "time", "offset", or "" for host time.
    const std::string from = find_value(config, "time")     ? "time"
                             : find_value(config, "offset") ? "offset"
                                                            : "";
    Saf3019p::DateTime start{};
    try {
        start = acorn_rtc_start_time(config, now);
    } catch (const std::invalid_argument& e) {
        found.push_back({from, e.what()});
        return found;
    }
    if (holds(layout, start.year)) return found;

    const std::string year = std::to_string(start.year);
    const std::string range = layout_holds(manifest, layout_name, layout);
    if (from == "time") {
        found.push_back({"time", "The clock cannot start in " + year + ": " + range + "."});
    } else if (from == "offset") {
        found.push_back({"offset", "With this offset the clock would start in " + year +
                                       " by this computer's clock today, but " + range + "."});
    } else {
        found.push_back({"", "This computer's clock says " + year + ", but " + range +
                                 "; give a start time or an offset, or choose another layout."});
    }
    return found;
}

}  // namespace beebium

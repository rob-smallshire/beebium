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

#ifndef BEEBIUM_ACORN_RTC_CONFIG_HPP
#define BEEBIUM_ACORN_RTC_CONFIG_HPP

#include "Saf3019p.hpp"

#include <beebium/extension/ConfigValidation.hpp>

#include <map>
#include <string>
#include <vector>

namespace beebium {

// The reading of an Acorn RTC instance's configuration, shared by the
// extension's start-up and its pre-launch check so the two cannot disagree.

// The register layout the config's `layout` names. The framework fills in the
// manifest default (Level 3 File Server 1.26's), so a config always names one.
// Throws std::invalid_argument for a config naming none or an unknown name.
RegisterLayout acorn_rtc_layout(const std::map<std::string, std::string>& config);

// The time the clock starts at: the config's `time`, or `now` moved by its
// `offset`, or `now` when it gives neither. Throws std::invalid_argument for a
// value that does not parse.
Saf3019p::DateTime acorn_rtc_start_time(const std::map<std::string, std::string>& config,
                                        const Saf3019p::DateTime& now);

// What is wrong with the config before launch, the clock reading `now`: a
// `time` or `offset` that does not parse, or a start whose year the layout
// cannot hold, is an error -- at the time, at the offset, or on the instance
// for host time. A start from an offset or host time is judged by the clock at
// the time of the check, as the launch judges it by the clock at launch.
// `manifest` names the layouts by their choice labels.
std::vector<ConfigDiagnostic> check_acorn_rtc_config(
    const ExtensionManifest& manifest, const std::map<std::string, std::string>& config,
    const Saf3019p::DateTime& now);

}  // namespace beebium

#endif  // BEEBIUM_ACORN_RTC_CONFIG_HPP

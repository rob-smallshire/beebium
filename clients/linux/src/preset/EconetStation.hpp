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

#ifndef BEEBIUM_LINUX_PRESET_ECONET_STATION_HPP
#define BEEBIUM_LINUX_PRESET_ECONET_STATION_HPP

#include "preset/PresetSchema.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace beebium::linux_client {

// A preset's Econet station (docs/preset-schema.md, Econet): a number, or
// chosen at launch from the whole range or a range of the user's.
struct EconetStation {
    enum class Mode { number, automatic, range };

    Mode mode = Mode::number;
    std::uint32_t number = 1;
    std::uint32_t low = 1;
    std::uint32_t high = 253;

    // A station as the preset gives it; the schema's range defaults the rest.
    static EconetStation from_json(const nlohmann::ordered_json& value, const SchemaStation& schema);
    // 80, "auto" or "auto:10-20".
    nlohmann::ordered_json to_json() const;
    // A number typed in, held within the schema's bounds; unchanged when
    // it is not a number.
    static std::uint32_t clamp(const std::string& text, std::uint32_t was, const SchemaStation& schema);

    friend bool operator==(const EconetStation&, const EconetStation&) = default;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_ECONET_STATION_HPP

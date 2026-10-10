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

#ifndef BEEBIUM_TIME_PARSER_HPP
#define BEEBIUM_TIME_PARSER_HPP

#include "Saf3019p.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace beebium {

// Parse an ISO 8601 datetime string into a DateTime.
// Accepts: "1985-03-15T10:30" or "1985-03-15T10:30:00"
// Throws std::invalid_argument on parse failure.
Saf3019p::DateTime parse_absolute_time(std::string_view iso8601);

// Apply a relative offset to the current local time and return the result.
// Accepts: "-10y", "+5h", "-365d", "+30m", "-1y6m" (combined)
// Units: y=years, M=months, d=days, h=hours, m=minutes
// Throws std::invalid_argument on parse failure.
Saf3019p::DateTime apply_relative_offset(std::string_view offset);

// Apply a relative offset, as above, to `base` rather than the current time.
Saf3019p::DateTime apply_relative_offset(std::string_view offset, Saf3019p::DateTime base);

// Get the current local time as a DateTime.
Saf3019p::DateTime current_local_datetime();

}  // namespace beebium

#endif  // BEEBIUM_TIME_PARSER_HPP

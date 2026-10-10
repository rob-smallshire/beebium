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

#include "TimeParser.hpp"

#include <chrono>
#include <ctime>
#include <charconv>

namespace beebium {

Saf3019p::DateTime parse_absolute_time(std::string_view iso8601) {
    // Parse "YYYY-MM-DDThh:mm" or "YYYY-MM-DDThh:mm:ss"
    // Minimal parser -- no timezone support (times are treated as local)

    Saf3019p::DateTime dt{};

    // Accept two formats:
    //   "YYYY-MM-DDThh:mm" (16+ chars, with colon) - standard ISO 8601
    //   "YYYY-MM-DDThhmm"  (15 chars, no colon)    - compact ISO 8601 (CLI-friendly)

    if (iso8601.size() < 15) {
        throw std::invalid_argument(
            "Invalid datetime format: expected YYYY-MM-DDThhmm or YYYY-MM-DDThh:mm, got '"
            + std::string(iso8601) + "'");
    }

    auto parse_int = [&](size_t pos, size_t len) -> int {
        int val = 0;
        auto [ptr, ec] = std::from_chars(iso8601.data() + pos, iso8601.data() + pos + len, val);
        if (ec != std::errc{}) {
            throw std::invalid_argument(
                "Invalid number in datetime at position " + std::to_string(pos));
        }
        return val;
    };

    dt.year = parse_int(0, 4);
    dt.month = parse_int(5, 2);
    dt.day = parse_int(8, 2);

    // Validate date separators
    if (iso8601[4] != '-' || iso8601[7] != '-' ||
        (iso8601[10] != 'T' && iso8601[10] != 't')) {
        throw std::invalid_argument(
            "Invalid datetime format: expected YYYY-MM-DDThhmm, got '" + std::string(iso8601) + "'");
    }

    // Detect compact (no colon) vs standard (with colon) time format
    if (iso8601.size() >= 16 && iso8601[13] == ':') {
        // Standard: "YYYY-MM-DDThh:mm"
        dt.hour = parse_int(11, 2);
        dt.minute = parse_int(14, 2);
    } else {
        // Compact: "YYYY-MM-DDThhmm"
        dt.hour = parse_int(11, 2);
        dt.minute = parse_int(13, 2);
    }

    // Basic range validation
    if (dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > 31 ||
        dt.hour < 0 || dt.hour > 23 || dt.minute < 0 || dt.minute > 59) {
        throw std::invalid_argument("Datetime values out of range: '" + std::string(iso8601) + "'");
    }

    return dt;
}

Saf3019p::DateTime apply_relative_offset(std::string_view offset) {
    return apply_relative_offset(offset, current_local_datetime());
}

Saf3019p::DateTime apply_relative_offset(std::string_view offset, Saf3019p::DateTime base) {
    // Parse offset string like "-10y", "+5h", "-365d", "+30m", "-1y6M"
    // Start from `base` and adjust

    auto dt = base;

    if (offset.empty()) {
        throw std::invalid_argument("Empty offset string");
    }

    int sign = 1;
    size_t pos = 0;

    if (offset[0] == '-') {
        sign = -1;
        pos = 1;
    } else if (offset[0] == '+') {
        pos = 1;
    }

    while (pos < offset.size()) {
        // Parse a number
        int value = 0;
        auto [ptr, ec] = std::from_chars(offset.data() + pos, offset.data() + offset.size(), value);
        if (ec != std::errc{} || ptr == offset.data() + pos) {
            throw std::invalid_argument(
                "Invalid offset format at position " + std::to_string(pos) +
                ": '" + std::string(offset) + "'");
        }
        pos = static_cast<size_t>(ptr - offset.data());

        if (pos >= offset.size()) {
            throw std::invalid_argument(
                "Missing unit suffix in offset: '" + std::string(offset) + "'");
        }

        char unit = offset[pos++];
        value *= sign;

        switch (unit) {
            case 'y': dt.year += value; break;
            case 'M': dt.month += value; break;
            case 'd': dt.day += value; break;
            case 'h': dt.hour += value; break;
            case 'm': dt.minute += value; break;
            default:
                throw std::invalid_argument(
                    "Unknown unit '" + std::string(1, unit) +
                    "' in offset (use y/M/d/h/m): '" + std::string(offset) + "'");
        }
    }

    // Normalise: carry minutes→hours→days→months→years
    while (dt.minute < 0) { dt.minute += 60; dt.hour--; }
    while (dt.minute >= 60) { dt.minute -= 60; dt.hour++; }
    while (dt.hour < 0) { dt.hour += 24; dt.day--; }
    while (dt.hour >= 24) { dt.hour -= 24; dt.day++; }
    // Month normalisation (months are 1-based)
    while (dt.month <= 0) { dt.month += 12; dt.year--; }
    while (dt.month > 12) { dt.month -= 12; dt.year++; }
    // Day normalisation is complex (varying month lengths); skip for now.
    // The caller validates via Saf3019p::initialise().

    return dt;
}

Saf3019p::DateTime current_local_datetime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};

#if defined(_WIN32)
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif

    return {
        .year = local_tm.tm_year + 1900,
        .month = local_tm.tm_mon + 1,
        .day = local_tm.tm_mday,
        .hour = local_tm.tm_hour,
        .minute = local_tm.tm_min
    };
}

}  // namespace beebium

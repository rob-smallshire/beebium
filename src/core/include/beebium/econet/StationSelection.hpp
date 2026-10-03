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

#pragma once

// Pure helpers for choosing an Econet station number at launch (issue #67).
//
// Two concerns, both free of I/O so they unit-test directly:
//   * parsing the `--station` / preset `econet.station` value, which is either
//     a fixed number or the word `auto` with an optional `lo-hi` range; and
//   * picking the lowest free number in that range given the set of numbers
//     already in use.
//
// The live part -- browsing `_aun._udp`, claiming a number and settling a
// collision -- lives in the AUN transport extension, which calls
// lowest_free_station() with the occupied set it gathered.

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace beebium::econet {

// An inclusive range of candidate station numbers, lo <= hi, both in 1..254.
struct StationRange {
    std::uint8_t lo = 1;
    std::uint8_t hi = 253;
};

// The one named default auto range. 1-253: station 0 and 255 are reserved and
// 254 is the conventional file server, so 1-253 is every usable number; starting
// at 1 also keeps an automatic client clear of the built-in fixed presets
// (80, 81) and the file server (254) in practice.
inline constexpr StationRange kAutoStationDefaultRange{};

// A parsed `--station` value: a fixed number, or an automatic choice within a
// range. `fixed` is meaningful only when !is_auto; `range` only when is_auto.
struct StationSpec {
    bool is_auto = false;
    std::uint8_t fixed = 0;
    StationRange range{};
};

// Parse a `--station` / preset `econet.station` value.
//   "1".."254"       -> a fixed station
//   "auto"           -> automatic, default range 1-253
//   "auto:<lo>-<hi>" -> automatic, the given inclusive range
// Returns the spec, or nullopt with a human-readable reason in `error` for an
// out-of-range number, a malformed range, lo > hi, or any other bad form.
inline std::optional<StationSpec> parse_station_spec(std::string_view value,
                                                     std::string& error) {
    auto parse_u8_in_range = [&](std::string_view text, int lo, int hi,
                                 const char* what, std::uint8_t& out) -> bool {
        if (text.empty()) {
            error = std::string("empty ") + what;
            return false;
        }
        int n = 0;
        for (char c : text) {
            if (c < '0' || c > '9') {
                error = std::string("non-numeric ") + what + " '" +
                        std::string(text) + "'";
                return false;
            }
            n = n * 10 + (c - '0');
            if (n > 999) {  // clamp the running value so a long run can't wrap
                n = 999;
            }
        }
        if (n < lo || n > hi) {
            error = std::string(what) + " " + std::string(text) + " out of range " +
                    std::to_string(lo) + "-" + std::to_string(hi);
            return false;
        }
        out = static_cast<std::uint8_t>(n);
        return true;
    };

    // Case-insensitive match of the "auto" keyword prefix.
    auto starts_with_auto = [](std::string_view s) {
        if (s.size() < 4) {
            return false;
        }
        auto lower = [](char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        };
        return lower(s[0]) == 'a' && lower(s[1]) == 'u' && lower(s[2]) == 't' &&
               lower(s[3]) == 'o';
    };

    if (!starts_with_auto(value)) {
        StationSpec spec;
        spec.is_auto = false;
        if (!parse_u8_in_range(value, 1, 254, "station", spec.fixed)) {
            return std::nullopt;
        }
        return spec;
    }

    // "auto" or "auto:lo-hi".
    std::string_view rest = value.substr(4);
    StationSpec spec;
    spec.is_auto = true;
    if (rest.empty()) {
        spec.range = kAutoStationDefaultRange;
        return spec;
    }
    if (rest.front() != ':') {
        error = "expected 'auto' or 'auto:<lo>-<hi>', got '" + std::string(value) + "'";
        return std::nullopt;
    }
    rest.remove_prefix(1);  // drop ':'
    auto dash = rest.find('-');
    if (dash == std::string_view::npos) {
        error = "auto range must be '<lo>-<hi>', got '" + std::string(rest) + "'";
        return std::nullopt;
    }
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    if (!parse_u8_in_range(rest.substr(0, dash), 1, 254, "auto range low", lo)) {
        return std::nullopt;
    }
    if (!parse_u8_in_range(rest.substr(dash + 1), 1, 254, "auto range high", hi)) {
        return std::nullopt;
    }
    if (lo > hi) {
        error = "auto range low " + std::to_string(lo) + " is above high " +
                std::to_string(hi);
        return std::nullopt;
    }
    spec.range = StationRange{lo, hi};
    return spec;
}

// The first number not in `occupied`, scanning from `start` upward and WRAPPING
// from range.hi back to range.lo, so the whole range is tried exactly once
// (issue #161: monotonic allocation that spaces re-use out). `start` outside the
// range is treated as range.lo. nullopt when every number in the range is taken.
inline std::optional<std::uint8_t> lowest_free_station_from(
    const std::set<std::uint8_t>& occupied, StationRange range,
    std::uint8_t start) {
    if (start < range.lo || start > range.hi) {
        start = range.lo;
    }
    const int span = static_cast<int>(range.hi) - static_cast<int>(range.lo) + 1;
    for (int i = 0; i < span; ++i) {
        const int n = static_cast<int>(range.lo) +
                      ((static_cast<int>(start) - static_cast<int>(range.lo) + i) % span);
        if (occupied.find(static_cast<std::uint8_t>(n)) == occupied.end()) {
            return static_cast<std::uint8_t>(n);
        }
    }
    return std::nullopt;
}

// The lowest number in [range.lo, range.hi] not present in `occupied`, or
// nullopt when every number in the range is taken (the range is exhausted).
inline std::optional<std::uint8_t> lowest_free_station(
    const std::set<std::uint8_t>& occupied, StationRange range) {
    return lowest_free_station_from(occupied, range, range.lo);
}

}  // namespace beebium::econet

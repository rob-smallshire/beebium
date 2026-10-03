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

// Per-host hint for monotonic automatic station allocation (issue #161). A tiny
// file in the per-user Beebium directory records the last number allocated on
// this host, so each launch starts its search one past the last rather than at
// the range's bottom -- spreading re-use out so a new machine does not silently
// inherit a number another machine freed seconds ago.
//
// The file is only a HINT for spacing launches on one host; the mDNS claim under
// the #147 ordering remains the arbiter for same-instant and cross-host races.
// So a lost update here is harmless. It must NEVER fail a launch: any error
// (missing directory, read-only home, corrupt contents, lock timeout) leaves
// the caller to fall back to lowest-free. The read-advance-write is done under a
// short exclusive lock (platform::with_locked_file); the lock is held only for
// that, never across the multi-second mDNS observation.

#include "StationSelection.hpp"
#include "../PlatformUtils.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace beebium::econet {

namespace detail {

// Parse a station number (1..254) from the file's text, or nullopt if absent,
// empty, non-numeric or out of range (a corrupt file).
inline std::optional<std::uint8_t> parse_station_file(const std::string& text) {
    std::string trimmed;
    for (char c : text) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        trimmed.push_back(c);
    }
    if (trimmed.empty()) {
        return std::nullopt;
    }
    int value = 0;
    const char* first = trimmed.data();
    const char* last = first + trimmed.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last || value < 1 || value > 254) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

inline std::filesystem::path lock_filepath_for(
    const std::filesystem::path& state_filepath) {
    return state_filepath.string() + ".lock";
}

}  // namespace detail

// Choose this launch's starting station: read the stored last number, advance
// it (wrapping within `range`), write the advanced value back, and return it.
// A missing file is the first run and starts at range.lo. Returns nullopt --
// the signal to fall back to lowest-free -- when the directory/file cannot be
// used, the lock is not taken within `timeout`, or the contents are corrupt; a
// corrupt file is reset to range.lo so a later launch recovers. The whole
// operation is under the lock.
inline std::optional<std::uint8_t> advance_auto_station_start(
    const std::filesystem::path& state_filepath, StationRange range,
    std::chrono::milliseconds timeout) {
    std::error_code ec;
    std::filesystem::create_directories(state_filepath.parent_path(), ec);
    // Ignore ec here: if the directory truly cannot be made, the lock open
    // below fails and we return nullopt.

    std::optional<std::uint8_t> result;
    bool corrupt = false;
    const bool ran = platform::with_locked_file(
        detail::lock_filepath_for(state_filepath), timeout, [&] {
            std::uint8_t start = range.lo;
            std::error_code exists_ec;
            const bool present =
                std::filesystem::exists(state_filepath, exists_ec);
            if (present) {
                std::ifstream in(state_filepath, std::ios::binary);
                std::string text((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
                auto last = detail::parse_station_file(text);
                if (!last.has_value()) {
                    corrupt = true;  // reset below, signal fallback this launch
                    start = range.lo;
                } else if (*last < range.lo || *last > range.hi) {
                    start = range.lo;  // stored value is outside this range
                } else {
                    start = (*last == range.hi)
                                ? range.lo
                                : static_cast<std::uint8_t>(*last + 1);
                }
            }
            std::ofstream out(state_filepath,
                              std::ios::binary | std::ios::trunc);
            if (out) {
                out << static_cast<unsigned>(start) << "\n";
            }
            if (!corrupt && out) {
                result = start;
            }
        });
    (void)ran;
    (void)corrupt;
    return result;
}

// Record the number actually taken, so the next launch continues past it.
// Best-effort: any failure (lock, write) is ignored -- the hint simply lapses.
inline void record_auto_station(const std::filesystem::path& state_filepath,
                                std::uint8_t station,
                                std::chrono::milliseconds timeout) {
    platform::with_locked_file(
        detail::lock_filepath_for(state_filepath), timeout, [&] {
            std::ofstream out(state_filepath,
                              std::ios::binary | std::ios::trunc);
            if (out) {
                out << static_cast<unsigned>(station) << "\n";
            }
        });
}

}  // namespace beebium::econet

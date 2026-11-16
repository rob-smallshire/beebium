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

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace beebium::cassette_detail {

// The container zlib should expect.
enum class Container { Gzip, Zlib };

// Inflate a whole gzip member or zlib stream. Returns nullopt if the data is
// not a valid stream of that kind.
std::optional<std::vector<uint8_t>> inflate_all(std::span<const uint8_t> input, Container container);

// Deflate a whole buffer as one gzip member, with no name and a zero time, so
// the same bytes always give the same file.
std::vector<uint8_t> deflate_gzip(std::span<const uint8_t> input);

inline bool has_gzip_magic(std::span<const uint8_t> bytes) {
    return bytes.size() >= 2 && bytes[0] == 0x1F && bytes[1] == 0x8B;
}

}  // namespace beebium::cassette_detail

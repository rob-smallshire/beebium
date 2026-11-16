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

#include "beebium/cassette/TapeImage.hpp"

#include "beebium/cassette/CswTape.hpp"
#include "beebium/cassette/UefTape.hpp"

#include "Inflate.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace beebium {

namespace {

bool starts_with(std::span<const uint8_t> bytes, const char* text, size_t length) {
    return bytes.size() >= length && std::memcmp(bytes.data(), text, length) == 0;
}

}  // namespace

std::unique_ptr<TapeSignal> open_tape_image(std::span<const uint8_t> bytes, const std::string& name) {
    if (starts_with(bytes, "UEF File!", 10) || cassette_detail::has_gzip_magic(bytes)) {
        // A gzipped file is a UEF if anything: UefTape inflates it and says
        // so if it is not.
        return UefTape::open(bytes, name);
    }
    if (starts_with(bytes, "Compressed Square Wave\x1A", 23)) {
        return CswTape::open(bytes, name);
    }
    throw std::runtime_error("The file " + name + " is not a UEF or CSW tape image.");
}

std::unique_ptr<TapeSignal> open_tape_file(const std::filesystem::path& filepath) {
    const std::string name = filepath.filename().string();
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("The tape image " + filepath.string() + " could not be opened.");
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return open_tape_image(bytes, name);
}

}  // namespace beebium

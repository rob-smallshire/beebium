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

#include "TapeSignal.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace beebium {

// Open a tape image, judging its format by content: a UEF (plain, or gzipped
// with the gzip magic) or a CSW. Throws std::runtime_error with a sentence
// naming `name` if it is neither or cannot be read.
std::unique_ptr<TapeSignal> open_tape_image(std::span<const uint8_t> bytes, const std::string& name);

// Read a tape image file and open it as above, naming it by its file name.
std::unique_ptr<TapeSignal> open_tape_file(const std::filesystem::path& filepath);

}  // namespace beebium

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


// The OS ROM (the MOS) a machine was started with, as SidewaysService's
// GetOsRom reports it: where its image came from and what it is.

#pragma once

#include <cstdint>
#include <string>

namespace beebium {

struct OsRom {
    std::string image_uri;      // the resolved image file, an absolute path
    std::string rom_name;       // the bare name found on the ROM search path, or empty
    std::uint32_t size_bytes = 0;
    std::string title;          // the manifest's title, else the file's stem
};

}  // namespace beebium

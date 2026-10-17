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

// How a reference to a file -- a disc image, a ROM, any file path parameter --
// is turned into the file it names. A reference is written in a container: a
// preset names files relative to the directory holding it, a command line
// relative to the working directory.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace beebium::server {

// True when `reference` names an absolute location: an absolute path on this
// platform, or a path starting with '/', which a preset may use on every
// platform (std::filesystem holds "/x" relative on Windows, lacking a drive).
inline bool is_absolute_reference(std::string_view reference) {
    return (!reference.empty() && reference.front() == '/') ||
           std::filesystem::path(reference).is_absolute();
}

// A reference anchored in its container: a relative path with a directory
// component ("discs/l3fs.dat", "../roms/x.rom") is joined to the container's
// directory and normalised. Everything else is returned as given: an absolute
// path, a URI, the empty string, and a bare name (no directory component),
// which names a file found by search rather than by position, or a keyword
// such as "none".
inline std::string anchor_reference(const std::string& reference,
                                    const std::filesystem::path& container_dirpath) {
    if (reference.empty() || reference.find("://") != std::string::npos ||
        is_absolute_reference(reference)) {
        return reference;
    }
    const std::filesystem::path path(reference);
    if (!path.has_parent_path()) {
        return reference;
    }
    return (container_dirpath / path).lexically_normal().string();
}

} // namespace beebium::server

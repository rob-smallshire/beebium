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

#ifndef BEEBIUM_SERVER_SYSTEM_DIRECTORIES_HPP
#define BEEBIUM_SERVER_SYSTEM_DIRECTORIES_HPP

#include "beebium/server/Platform.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace beebium::server {

// The directory of a kind of resource shipped with the servers, named
// `dirname` (presets, blank-discs), found in this order:
//   1. $BEEBIUM_SERVERS_DIRPATH/<dirname>/
//   2. <dirname>/ beside the executable (build directory layout)
//   3. ../share/beebium/<dirname>/ relative to the executable (installed layout)
// or nullopt when none of them is a directory.
inline std::optional<std::filesystem::path> find_system_dirpath(std::string_view dirname) {
    if (auto env_dirpath = platform::get_env("BEEBIUM_SERVERS_DIRPATH")) {
        std::filesystem::path dirpath = std::filesystem::path(*env_dirpath) / dirname;
        if (std::filesystem::is_directory(dirpath)) {
            return dirpath;
        }
    }

    // Anchor the relative lookups on the executable's real on-disk directory
    // (symlinks resolved, so a bin/ symlink into an installed tree still finds
    // that tree's share/). Fall back to the current directory when the OS
    // cannot report the executable's location.
    auto exe_dirpath = beebium::platform::executable_directory()
                           .value_or(std::filesystem::current_path());

    auto build_dirpath = exe_dirpath / dirname;
    if (std::filesystem::is_directory(build_dirpath)) {
        return build_dirpath;
    }

    auto installed_dirpath = exe_dirpath.parent_path() / "share" / "beebium" / dirname;
    if (std::filesystem::is_directory(installed_dirpath)) {
        return installed_dirpath;
    }

    return std::nullopt;
}

// The blank disc templates shipped with the servers.
inline std::optional<std::filesystem::path> find_blank_discs_dirpath() {
    return find_system_dirpath("blank-discs");
}

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SYSTEM_DIRECTORIES_HPP

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
// is turned into the file it names. A reference is resolved from where it is
// written, its container: a preset's directory for a preset, the working
// directory for the command line (values given with --preset resolve as if
// written in the preset). The rule (docs/preset-schema.md, "File references"):
//
//   1. A bare name (no directory component) is looked up on a search path:
//      the container, then the per-user directory for that kind of resource
//      (discs/, roms/), then the system directories for it.
//   2. A relative path with a directory component resolves against the
//      container.
//   3. An absolute path is used as is; a leading '/' is absolute everywhere.
//   4. A leading "~/" (or "~\") is the home directory, before 2 and 3.
//
// A reference to a location rather than a resource (a file the extension owns
// and may create, such as the AUN map file) follows 2, 3 and 4 but is never
// searched for.

#pragma once

#include <beebium/PlatformUtils.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace beebium::server {

// True when `reference` names an absolute location: an absolute path on this
// platform, or a path starting with '/', which a preset may use on every
// platform (std::filesystem holds "/x" relative on Windows, lacking a drive).
inline bool is_absolute_reference(std::string_view reference) {
    return (!reference.empty() && reference.front() == '/') ||
           std::filesystem::path(reference).is_absolute();
}

// The directory a leading "~" stands for: HOME, else USERPROFILE, on every
// platform; nullopt when neither is set.
inline std::optional<std::filesystem::path> home_dirpath() {
    for (const char* name : {"HOME", "USERPROFILE"}) {
        if (auto value = beebium::platform::get_env(name); value && !value->empty()) {
            return std::filesystem::path(*value);
        }
    }
    return std::nullopt;
}

// A reference anchored in its container: a leading "~/" or "~\" becomes the
// home directory, and a relative path with a directory component
// ("discs/l3fs.dat", "../roms/x.rom") is joined to the container's directory;
// both are normalised. Everything else is returned as given: an absolute
// path, a URI, the empty string, and a bare name (no directory component),
// which names a file found by search rather than by position, or a keyword
// such as "none".
inline std::string anchor_reference(const std::string& reference,
                                    const std::filesystem::path& container_dirpath) {
    if (reference.size() >= 2 && reference[0] == '~' &&
        (reference[1] == '/' || reference[1] == '\\')) {
        if (auto home = home_dirpath()) {
            return (*home / reference.substr(2)).lexically_normal().string();
        }
    }
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

// The file called `name` (a bare name) in the first of `search_dirpaths` that
// holds one. Throws when none does, naming every directory searched, in
// order; `what` names the kind of file for the message ("ROM").
inline std::filesystem::path find_on_search_path(
    std::string_view name, const std::vector<std::filesystem::path>& search_dirpaths,
    std::string_view what) {
    for (const auto& dirpath : search_dirpaths) {
        const auto filepath = dirpath / std::filesystem::path(name);
        std::error_code ec;
        if (std::filesystem::is_regular_file(filepath, ec)) {
            return filepath;
        }
    }
    std::string message = std::string(what) + " '" + std::string(name) + "' not found; searched ";
    for (std::size_t i = 0; i < search_dirpaths.size(); ++i) {
        message += (i == 0 ? "" : ", ") + search_dirpaths[i].string();
    }
    throw std::runtime_error(message);
}

// True when `reference` is a bare name: no directory component, not absolute,
// not a URI -- the form that is searched for.
inline bool is_bare_reference(const std::string& reference) {
    return !reference.empty() && reference.find("://") == std::string::npos &&
           !is_absolute_reference(reference) &&
           !std::filesystem::path(reference).has_parent_path();
}

// A reference given on a command line, as a preset records what it meant: a
// path anchored in its container, and a bare name the container holds made
// the path to that file, whose meaning is that file. A bare name the
// container does not hold is kept: its meaning is the search, which still
// holds wherever the preset is moved.
inline std::string command_line_reference(const std::string& reference,
                                          const std::filesystem::path& container_dirpath) {
    if (!is_bare_reference(reference)) {
        return anchor_reference(reference, container_dirpath);
    }
    const auto filepath = container_dirpath / reference;
    std::error_code ec;
    return std::filesystem::is_regular_file(filepath, ec) ? filepath.lexically_normal().string()
                                                          : reference;
}

// The kinds of resource a bare name is searched for, each with its per-user
// directory.
enum class ResourceKind { Disc, Rom };

inline std::filesystem::path user_resource_dirpath(ResourceKind kind) {
    return beebium::platform::user_state_base_dirpath() /
           (kind == ResourceKind::Disc ? "discs" : "roms");
}

// The file a reference names (the rule above): anchored in its container
// unless it is a bare name, which is found on the search path of the
// container, the kind's per-user directory and then `system_dirpaths`. A
// per-user ROM that hides a system ROM of the same name is reported on
// stderr, naming both. Throws when a bare name is found nowhere, naming every
// directory searched. Whether an anchored path exists is the caller's
// concern.
inline std::filesystem::path resolve_reference(
    const std::string& reference, ResourceKind kind,
    const std::filesystem::path& container_dirpath,
    const std::vector<std::filesystem::path>& system_dirpaths) {
    if (!is_bare_reference(reference)) {
        return anchor_reference(reference, container_dirpath);
    }
    const auto user_dirpath = user_resource_dirpath(kind);
    std::vector<std::filesystem::path> search_dirpaths{container_dirpath, user_dirpath};
    search_dirpaths.insert(search_dirpaths.end(), system_dirpaths.begin(),
                           system_dirpaths.end());
    const auto found = find_on_search_path(
        reference, search_dirpaths, kind == ResourceKind::Disc ? "Disc image" : "ROM");
    if (kind == ResourceKind::Rom && found.parent_path() == user_dirpath) {
        for (const auto& dirpath : system_dirpaths) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(dirpath / reference, ec)) {
                std::cerr << "Note: ROM " << reference << " is " << found.string()
                          << ", which takes the place of " << (dirpath / reference).string()
                          << "\n";
                break;
            }
        }
    }
    return found;
}

} // namespace beebium::server

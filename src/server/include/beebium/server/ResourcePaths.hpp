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
//
// Anchoring is lexical, on the container's directory. Whether a file is
// there, and reading or copying it, goes through the ReferenceContainer, so a
// file the container holds is reached the same way whatever holds it.

#pragma once

#include <beebium/PlatformUtils.hpp>
#include <beebium/server/AtomicFile.hpp>
#include <beebium/zip/ZipArchive.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace beebium::server {

// Where a launch's file references are written: a preset's directory or
// archive, or the working directory for the command line. Every check that a
// referenced file exists, and every read or copy of one, goes through it: a
// file within the container is one it holds, and a file elsewhere is an
// ordinary file.
//
// An archive (a stored preset) is anchored on its own path as if it were a
// directory: "discs/x.ssd" written in /p/a.beebium-preset names the member
// discs/x.ssd, at /p/a.beebium-preset/discs/x.ssd, and "../x.ssd" the file
// /p/x.ssd beside it, as for a directory preset.
class ReferenceContainer {
public:
    // A directory is a container.
    ReferenceContainer(std::filesystem::path dirpath) : dirpath_(std::move(dirpath)) {}

    // The zip archive at `archive_filepath`, read through `archive`.
    static ReferenceContainer archive(std::filesystem::path archive_filepath,
                                      std::shared_ptr<const zip::ZipReader> archive) {
        ReferenceContainer container(std::move(archive_filepath));
        container.archive_ = std::move(archive);
        return container;
    }

    // The directory references are anchored in.
    const std::filesystem::path& dirpath() const { return dirpath_; }

    // `filepath` relative to the container, when it lies within it (by its
    // name, whether or not there is a file there).
    std::optional<std::filesystem::path> relative_path_of(
        const std::filesystem::path& filepath) const {
        auto relative = filepath.lexically_normal().lexically_relative(dirpath_.lexically_normal());
        if (relative.empty() || *relative.begin() == "..") return std::nullopt;
        return relative;
    }

    // True when there is a file at `filepath`.
    bool has_file(const std::filesystem::path& filepath) const {
        if (auto member = member_name_of(filepath)) return archive_->contains(*member);
        std::error_code ec;
        return std::filesystem::is_regular_file(filepath, ec);
    }

    // The bytes of the file at `filepath`. Throws when it cannot be read.
    std::vector<std::uint8_t> read_file(const std::filesystem::path& filepath) const {
        if (auto member = member_name_of(filepath)) return archive_->read(*member);
        std::ifstream in(filepath, std::ios::binary);
        if (!in) throw std::runtime_error("cannot read " + filepath.string());
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    // A writable copy of the file at `filepath` at `copy_filepath`, replacing
    // it atomically (see write_file_atomically).
    void copy_file(const std::filesystem::path& filepath,
                   const std::filesystem::path& copy_filepath) const {
        const auto member = member_name_of(filepath);
        write_file_atomically(copy_filepath, [&](const std::filesystem::path& temp_filepath) {
            if (member) {
                archive_->extract(*member, temp_filepath);
            } else {
                std::filesystem::copy_file(filepath, temp_filepath);
            }
        });
    }

    // A real file holding what is at `filepath`, for a consumer that opens it
    // by path itself (an extension's file parameter): `filepath` itself,
    // unless it is a member of an archive, which is extracted into
    // `machine_dirpath` at its own relative path, once. Only a launch, which
    // has a machine directory, calls this; a check without one asks has_file.
    std::filesystem::path materialize(const std::filesystem::path& filepath,
                                      const std::filesystem::path& machine_dirpath) const {
        const auto relative = archive_ ? relative_path_of(filepath) : std::nullopt;
        if (!relative || !archive_->contains(relative->generic_string())) return filepath;
        const auto copy_filepath = machine_dirpath / *relative;
        std::error_code ec;
        if (!std::filesystem::exists(copy_filepath, ec)) {
            std::filesystem::create_directories(copy_filepath.parent_path());
            copy_file(filepath, copy_filepath);
        }
        return copy_filepath;
    }

private:
    // The member of the archive `filepath` names, when the container is an
    // archive and `filepath` lies within it.
    std::optional<std::string> member_name_of(const std::filesystem::path& filepath) const {
        if (!archive_) return std::nullopt;
        if (auto relative = relative_path_of(filepath)) return relative->generic_string();
        return std::nullopt;
    }

    std::filesystem::path dirpath_;
    std::shared_ptr<const zip::ZipReader> archive_;
};

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
// holds one, by `has_file`. Throws when none does, naming every directory
// searched, in order; `what` names the kind of file for the message ("ROM").
inline std::filesystem::path find_on_search_path(
    std::string_view name, const std::vector<std::filesystem::path>& search_dirpaths,
    std::string_view what,
    const std::function<bool(const std::filesystem::path&)>& has_file =
        [](const std::filesystem::path& filepath) {
            std::error_code ec;
            return std::filesystem::is_regular_file(filepath, ec);
        }) {
    for (const auto& dirpath : search_dirpaths) {
        const auto filepath = dirpath / std::filesystem::path(name);
        if (has_file(filepath)) {
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
                                          const ReferenceContainer& container) {
    if (!is_bare_reference(reference)) {
        return anchor_reference(reference, container.dirpath());
    }
    const auto filepath = container.dirpath() / reference;
    return container.has_file(filepath) ? filepath.lexically_normal().string() : reference;
}

// The kinds of resource a bare name is searched for, each with its per-user
// directory.
enum class ResourceKind { Disc, Rom };

inline std::filesystem::path user_resource_dirpath(ResourceKind kind) {
    return beebium::platform::user_state_base_dirpath() /
           (kind == ResourceKind::Disc ? "discs" : "roms");
}

// Where a reference's file was found: named by a path (anchored, not
// searched), or found by search in the container, the per-user directory for
// its kind, or a system directory. Who owns the file follows from it.
enum class ReferenceOrigin { Path, Container, User, System };

struct ResolvedReference {
    std::filesystem::path path;
    ReferenceOrigin origin;
};

// The file a reference names (the rule above), and where it was found:
// anchored in its container unless it is a bare name, which is found on the
// search path of the container, the kind's per-user directory and then
// `system_dirpaths`. A per-user ROM that hides a system ROM of the same name
// is reported on stderr, naming both. Throws when a bare name is found
// nowhere, naming every directory searched. Whether an anchored path exists
// is the caller's concern.
inline ResolvedReference resolve_reference(
    const std::string& reference, ResourceKind kind, const ReferenceContainer& container,
    const std::vector<std::filesystem::path>& system_dirpaths) {
    if (!is_bare_reference(reference)) {
        return {anchor_reference(reference, container.dirpath()), ReferenceOrigin::Path};
    }
    const auto user_dirpath = user_resource_dirpath(kind);
    std::vector<std::filesystem::path> search_dirpaths{container.dirpath(), user_dirpath};
    search_dirpaths.insert(search_dirpaths.end(), system_dirpaths.begin(),
                           system_dirpaths.end());
    const auto found = find_on_search_path(
        reference, search_dirpaths, kind == ResourceKind::Disc ? "Disc image" : "ROM",
        [&](const std::filesystem::path& filepath) { return container.has_file(filepath); });
    const auto origin = found.parent_path() == container.dirpath() ? ReferenceOrigin::Container
                        : found.parent_path() == user_dirpath    ? ReferenceOrigin::User
                                                                 : ReferenceOrigin::System;
    if (kind == ResourceKind::Rom && origin == ReferenceOrigin::User) {
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
    return {found, origin};
}

} // namespace beebium::server

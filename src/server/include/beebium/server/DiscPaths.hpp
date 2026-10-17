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

#ifndef BEEBIUM_SERVER_DISC_PATHS_HPP
#define BEEBIUM_SERVER_DISC_PATHS_HPP

#include <beebium/server/ResourcePaths.hpp>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// What a machine opens for a disc a launch names, and what it copies.
//
// A disc reference is resolved by the file reference rule (ResourcePaths.hpp):
// a bare name is searched for in the launch's container, then the per-user
// discs/. The media a template names -- found by search, or named by a path
// inside the template (a preset's directory or archive) -- are copied into the
// machine's own directory (MachineDirectory) and the copy is opened, so the
// guest's writes change the machine and never the template; the media a user
// names by location are opened in place. A hard disc's .dsc geometry sidecar
// sits beside its .dat and is copied with it.

// A hard-disc image format: the pair of files an Acorn SCSI hard disc is kept
// in. Floppy formats are DiscFormatRegistry's; this is their hard-disc
// counterpart, which list-disc-formats lists beside them.
struct HardDiscImageFormat {
    std::string_view name;
    std::string_view description;
    std::string_view extension;          // the file a user names, e.g. ".dat"
    std::string_view sidecar_extension;  // the file beside it, e.g. ".dsc"
    bool write_support;
};

inline constexpr HardDiscImageFormat kAcornScsiHardDiscFormat{
    "Acorn SCSI",
    "Acorn SCSI hard disc image: the sectors in a .dat file, with the drive's "
    "geometry in a .dsc file beside it",
    ".dat", ".dsc", true};

// Every hard-disc image format, in the order list-disc-formats gives them.
inline constexpr std::array<HardDiscImageFormat, 1> kHardDiscImageFormats{
    kAcornScsiHardDiscFormat};

class DiscPaths {
public:
    // Where a launch copies the media a template names: the machine's own
    // directory, and where the template it was launched from is anchored (the
    // preset's directory or archive), when it was launched from one.
    struct CopyDestination {
        std::filesystem::path dirpath;
        std::optional<std::filesystem::path> template_dirpath;
    };

    // The path the emulator should open for a disc reference (a floppy or a
    // hard disc) written in `container`. Media a template names by search or
    // by position within itself is the machine's own copy, made in
    // `destination`: a disc found by search -- in the container or the
    // per-user discs/ -- or named by a path inside the template, at the same
    // relative path. Media the user names by location elsewhere is the user's
    // own file, opened in place.
    static std::filesystem::path resolve_disc_image(
        const std::string& image, const ReferenceContainer& container,
        const CopyDestination& destination) {
        const auto found = resolve_reference(image, ResourceKind::Disc, container, {});
        std::filesystem::path relative_path;
        if (found.origin != ReferenceOrigin::Path) {
            relative_path = found.path.filename();
        } else if (destination.template_dirpath) {
            const auto inside = found.path.lexically_normal().lexically_relative(
                destination.template_dirpath->lexically_normal());
            if (!inside.empty() && *inside.begin() != "..") relative_path = inside;
        }
        if (relative_path.empty()) return found.path;
        return copy_into_machine(container, found.path, destination.dirpath / relative_path);
    }

    // A copy of `source`, read through `container`, with a hard-disc
    // image's geometry sidecar, at `copy_filepath`, made once: a second
    // reference to the same file finds the first copy. A source that does not
    // exist is returned as it is, for the caller to report.
    static std::filesystem::path copy_into_machine(const ReferenceContainer& container,
                                                   const std::filesystem::path& source,
                                                   const std::filesystem::path& copy_filepath) {
        if (!container.has_file(source)) return source;
        std::error_code ec;
        if (!std::filesystem::exists(copy_filepath, ec)) {
            std::filesystem::create_directories(copy_filepath.parent_path());
            container.copy_file(source, copy_filepath);
            if (source.extension() == kAcornScsiHardDiscFormat.extension) {
                auto source_sidecar = source;
                source_sidecar.replace_extension(kAcornScsiHardDiscFormat.sidecar_extension);
                auto copy_sidecar = copy_filepath;
                copy_sidecar.replace_extension(kAcornScsiHardDiscFormat.sidecar_extension);
                if (container.has_file(source_sidecar)) {
                    container.copy_file(source_sidecar, copy_sidecar);
                }
            }
        }
        return copy_filepath;
    }
};

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_DISC_PATHS_HPP

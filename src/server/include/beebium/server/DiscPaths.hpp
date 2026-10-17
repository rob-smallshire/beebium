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

#include <beebium/PlatformUtils.hpp>
#include <beebium/server/ResourcePaths.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if !defined(__APPLE__) && !defined(_WIN32)
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#endif

namespace beebium::server {

// Discovery of the shipped hard-disc masters, and what a machine copies.
//
// Beebium ships read-only MASTER disc images under share/beebium/discs (and
// the build tree's discs/). A master must never be opened writable or
// mutated: the emulated filing system writes to disc, and letting those
// writes land on a shipped image (or, worse, a signed/sealed app bundle) is
// how emulator disc images silently rot. A preset names a master by its
// BARE, version-named filename ("l3fs-v1_26b.dat").
//
// A disc reference is resolved by the file reference rule (ResourcePaths.hpp),
// with the master directory as the system directory for discs. The media a
// template names -- found by search, a master among them, or named by a path
// inside the template's directory -- are copied into the machine's own
// directory (MachineDirectory) and the copy is opened; the media a user names
// by location are opened in place. The .dsc geometry sidecar sits beside the
// .dat and is copied with it.

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
    // Find the master disc directory. Search order mirrors RomPaths:
    //   1. BEEBIUM_DISC_DIR environment variable
    //   2. a discs/ directory at or above the executable (build layout)
    //   3. ../share/beebium/discs relative to the executable (installed)
    // nullopt when none exists.
    static std::optional<std::filesystem::path> find_disc_directory() {
        if (auto env_dir = beebium::platform::get_env("BEEBIUM_DISC_DIR")) {
            std::filesystem::path env_path(*env_dir);
            if (std::filesystem::is_directory(env_path)) {
                return env_path;
            }
        }

        // Anchor the relative lookups on the executable's real on-disk
        // directory (symlinks resolved, so a bin/ symlink into an installed
        // tree still finds that tree's share/). Fall back to the current
        // directory when the OS cannot report the executable's location.
        auto exe_dirpath = beebium::platform::executable_directory()
                               .value_or(std::filesystem::current_path());

        {
            auto dir = exe_dirpath;
            for (int level = 0; level < 6; ++level) {
                auto build_discs = dir / "discs";
                if (std::filesystem::is_directory(build_discs)) {
                    return build_discs;
                }
                auto parent = dir.parent_path();
                if (parent == dir) {
                    break;
                }
                dir = parent;
            }
        }

        auto installed = exe_dirpath.parent_path() / "share" / "beebium" / "discs";
        if (std::filesystem::is_directory(installed)) {
            return installed;
        }

        return std::nullopt;
    }

    // A bundled hard-disc master: the bare name a preset or --scsi-hdd uses
    // to refer to it, its size, and the name a user sees.
    struct BundledImage {
        std::string image;            // e.g. "l3fs-v1_26b.dat"
        std::uintmax_t size_bytes = 0;
        std::string display_name;     // e.g. "Acorn Level 3 File Server 1.26"
    };

    // The hard-disc masters in a master directory: every <name>.dat that has
    // its <name>.dsc and a <name>.json giving its display name, sorted by
    // name. These are the bare names resolve_disc_image accepts. An absent
    // directory has none.
    static std::vector<BundledImage> bundled_hard_disc_images(
        const std::filesystem::path& master_dir) {
        std::vector<BundledImage> images;
        std::error_code ec;
        if (!std::filesystem::is_directory(master_dir, ec)) {
            return images;
        }
        for (const auto& entry : std::filesystem::directory_iterator(master_dir, ec)) {
            const auto& path = entry.path();
            if (path.extension() != kAcornScsiHardDiscFormat.extension ||
                !entry.is_regular_file(ec)) {
                continue;
            }
            auto dsc = path;
            dsc.replace_extension(kAcornScsiHardDiscFormat.sidecar_extension);
            if (!std::filesystem::exists(dsc, ec)) continue;
            auto display_name = master_display_name(path);
            if (!display_name) continue;
            images.push_back({path.filename().string(), entry.file_size(ec),
                              std::move(*display_name)});
        }
        std::sort(images.begin(), images.end(),
                  [](const BundledImage& a, const BundledImage& b) { return a.image < b.image; });
        return images;
    }

    // The display name a master's <name>.json gives (its "display_name"), or
    // nullopt when the file is missing or gives none.
    static std::optional<std::string> master_display_name(
        const std::filesystem::path& dat_filepath) {
        auto json_filepath = dat_filepath;
        json_filepath.replace_extension(".json");
        std::ifstream in(json_filepath);
        if (!in) return std::nullopt;
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (!j.is_object() || !j.contains("display_name") || !j["display_name"].is_string()) {
            return std::nullopt;
        }
        return j["display_name"].get<std::string>();
    }

    // The hard-disc masters this server ships (find_disc_directory), or none
    // when it has no disc directory.
    static std::vector<BundledImage> bundled_hard_disc_images() {
        const auto master_dirpath = find_disc_directory();
        return master_dirpath ? bundled_hard_disc_images(*master_dirpath)
                              : std::vector<BundledImage>{};
    }

    // Where a launch copies the media a template names: the machine's own
    // directory, and the directory of the template it was launched from (the
    // preset's), when it was launched from one.
    struct CopyDestination {
        std::filesystem::path dirpath;
        std::optional<std::filesystem::path> template_dirpath;
    };

    // The path the emulator should open for a disc reference (a floppy or a
    // hard disc) written in `container`. Media a template names by
    // search or by position within itself is the machine's own copy, made in
    // `destination`: a disc found by search -- in the container, the per-user
    // discs/ or among the masters -- or named by a path inside the template's
    // directory, at the same relative path. Media the user names by location
    // elsewhere is the user's own file, opened in place. A master is checked
    // to be one (its .dsc and display name) before it is copied.
    static std::filesystem::path resolve_disc_image(
        const std::string& image, const ReferenceContainer& container,
        const CopyDestination& destination) {
        std::vector<std::filesystem::path> system_dirpaths;
        if (auto master_dirpath = find_disc_directory()) system_dirpaths.push_back(*master_dirpath);
        const auto found =
            resolve_reference(image, ResourceKind::Disc, container, system_dirpaths);
        if (found.origin == ReferenceOrigin::System &&
            found.path.extension() == kAcornScsiHardDiscFormat.extension) {
            check_master(found.path);
            return copy_into_machine(container, found.path,
                                     destination.dirpath / found.path.filename());
        }
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

    // Throws unless `dat_filepath` is a whole master: its .dsc beside it and a
    // .json giving its display name.
    static void check_master(const std::filesystem::path& dat_filepath) {
        auto dsc_filepath = dat_filepath;
        dsc_filepath.replace_extension(kAcornScsiHardDiscFormat.sidecar_extension);
        if (!std::filesystem::exists(dsc_filepath)) {
            throw std::runtime_error("Bundled disc image " + dat_filepath.string() +
                                     " has no " + dsc_filepath.filename().string() +
                                     " beside it");
        }
        if (!master_display_name(dat_filepath)) {
            throw std::runtime_error(
                "Bundled disc image " + dat_filepath.string() + " has no display name: " +
                std::filesystem::path(dat_filepath).replace_extension(".json").string() +
                " must give one as {\"display_name\": \"...\"}");
        }
    }
};

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_DISC_PATHS_HPP

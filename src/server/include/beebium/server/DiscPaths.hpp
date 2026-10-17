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
#include <functional>
#include <optional>
#include <random>
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

// Discovery + copy-on-write resolution for bundled SCSI hard-disc images.
//
// Beebium ships read-only MASTER disc images under share/beebium/discs (and
// the build tree's discs/). A master must never be opened writable or
// mutated: the emulated filing system writes to disc, and letting those
// writes land on a shipped image (or, worse, a signed/sealed app bundle) is
// how emulator disc images silently rot. So a preset references a disc by a
// BARE, version-named filename ("l3fs-v1_26.dat"), and this resolver copies
// the master to a per-user working copy on first use and hands the emulator
// the WORKING copy -- never the master.
//
// A reference is resolved by the file reference rule (ResourcePaths.hpp),
// with the master directory as the system directory for discs. A file found
// in the container or the per-user discs/ is the user's own: opened in place,
// writable. A bare name that names a master, and is not in the container,
// resolves to a working copy of it (below).
//
// Working-copy modes:
//   * Persistent (real emulator sessions): the working copy lives in the
//     per-user discs directory (a sibling of the user presets directory, so
//     all Beebium per-user state co-locates). Copied from the master only if
//     absent; kept thereafter. "Reset to a good image" = delete the working
//     copy. Persistence across runs.
//   * Scratch (build-time create-preset/capture-screenshot boots): the
//     working copy lives in an ephemeral directory named by
//     BEEBIUM_DISC_WORK_DIR and is ALWAYS re-copied fresh from the master, so
//     a thumbnail boots from a pristine image every build (deterministic) and
//     never touches user state or the master.
//
// The .dsc geometry sidecar sits beside the .dat and is copied with it.

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

enum class DiscWorkMode {
    Persistent,  // per-user working copy, copy-if-absent, kept
    Scratch,     // ephemeral working copy, always refreshed from the master
};

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

    // Per-user working-copy directory: the per-user discs directory. Not
    // created here; prepare_working_image() creates it.
    static std::filesystem::path get_user_disc_working_dirpath() {
        return user_resource_dirpath(ResourceKind::Disc);
    }

    // Where a launch makes the working copy of a master, and how.
    struct CopyDestination {
        std::filesystem::path dirpath;
        DiscWorkMode mode;
    };

    // The copy destination a launch uses: BEEBIUM_DISC_WORK_DIR in Scratch
    // mode when it is set, else the per-user discs directory, Persistent.
    static CopyDestination launch_copy_destination() {
        if (auto env = beebium::platform::get_env("BEEBIUM_DISC_WORK_DIR")) {
            return {std::filesystem::path(*env), DiscWorkMode::Scratch};
        }
        return {get_user_disc_working_dirpath(), DiscWorkMode::Persistent};
    }

    // The path the emulator should open for a hard-disc "image" reference
    // written in `container_dirpath`, using find_disc_directory() as the
    // master source and making a master's working copy at `destination`.
    static std::filesystem::path resolve_disc_image(
        const std::string& image, const std::filesystem::path& container_dirpath,
        const CopyDestination& destination) {
        const auto master_dirpath = find_disc_directory();
        std::error_code ec;
        // Working copies are not user files: the per-user discs/ doubles as the
        // directory working copies are made in, so a file there named like a
        // master is taken to be that master's working copy, and the master's
        // name resolves through copy-on-write, never through the search (and
        // never with a notice). A consequence of the shared directory, to be
        // deleted when machine directories hold the working copies (#103a).
        if (is_bare_reference(image) && master_dirpath &&
            std::filesystem::is_regular_file(*master_dirpath / image, ec) &&
            !std::filesystem::is_regular_file(container_dirpath / image, ec)) {
            return prepare_working_image(image, *master_dirpath, destination.dirpath,
                                         destination.mode);
        }
        std::vector<std::filesystem::path> system_dirpaths;
        if (master_dirpath) system_dirpaths.push_back(*master_dirpath);
        return resolve_reference(image, ResourceKind::Disc, container_dirpath, system_dirpaths)
            .path;
    }

    // Core copy-on-write policy, with the master and working directories
    // supplied explicitly (so it is unit-testable without touching env or the
    // real per-user directory). `image` must be a bare filename.
    //
    //   * The master (master_dir/image + its .dsc) is only ever READ, as a
    //     copy source -- never returned, so it can never be opened writable.
    //   * Persistent: copy master -> working only if the working .dat/.dsc is
    //     absent; otherwise keep the existing working copy.
    //   * Scratch: always replace the working copy with a fresh master copy.
    //     Each file is replaced atomically (see copy_file_atomically), so a
    //     server mounting it -- or another preparation racing this one on a
    //     shared work directory -- sees the old image or the new, never a
    //     missing or partly written one.
    //   * The working copy is made writable regardless of the master's
    //     permissions (the master is shipped read-only).
    //
    // Returns the working .dat path (the emulator opens it read/write; the
    // .dsc sits beside it).
    static std::filesystem::path prepare_working_image(
        std::string_view image,
        const std::filesystem::path& master_dir,
        const std::filesystem::path& work_dir,
        DiscWorkMode mode) {
        std::filesystem::path dat_name(image);
        if (dat_name.has_parent_path()) {
            throw std::runtime_error(
                "prepare_working_image requires a bare filename, got: " +
                std::string(image));
        }
        std::filesystem::path dsc_name =
            std::filesystem::path(dat_name).replace_extension(
                kAcornScsiHardDiscFormat.sidecar_extension);

        const auto master_dat = master_dir / dat_name;
        const auto master_dsc = master_dir / dsc_name;
        if (!std::filesystem::exists(master_dat) ||
            !std::filesystem::exists(master_dsc)) {
            throw std::runtime_error(
                "Bundled disc image not found: " + master_dat.string() +
                " (with its .dsc sidecar)");
        }
        if (!master_display_name(master_dat)) {
            throw std::runtime_error(
                "Bundled disc image " + master_dat.string() + " has no display name: " +
                std::filesystem::path(master_dat).replace_extension(".json").string() +
                " must give one as {\"display_name\": \"...\"}");
        }

        const auto working_dat = work_dir / dat_name;
        const auto working_dsc = work_dir / dsc_name;

        const bool need_copy = (mode == DiscWorkMode::Scratch) ||
                               !std::filesystem::exists(working_dat) ||
                               !std::filesystem::exists(working_dsc);
        if (need_copy) {
            std::filesystem::create_directories(work_dir);
            copy_master_to_working(master_dat, working_dat);
            copy_master_to_working(master_dsc, working_dsc);
        }
        return working_dat;
    }

    using FileCopier =
        std::function<void(const std::filesystem::path&, const std::filesystem::path&)>;

    // Replace dst with a writable copy of src, atomically: copy to a
    // uniquely named temporary in dst's directory, make it writable (the
    // master is shipped read-only), then rename it over dst. A reader opening
    // dst sees the complete old file or the complete new one, and a copy that
    // fails part-way removes its temporary and leaves dst untouched.
    //
    // rename() replaces atomically on POSIX. On Windows std::filesystem
    // rename uses MoveFileEx with MOVEFILE_REPLACE_EXISTING, which replaces an
    // existing file but is not guaranteed atomic, and fails -- leaving dst
    // intact -- if another process has dst open without delete sharing.
    //
    // `copy` performs the copy into the temporary; tests substitute one that
    // fails part-way.
    static void copy_file_atomically(const std::filesystem::path& src,
                                     const std::filesystem::path& dst,
                                     const FileCopier& copy = copy_file_plain) {
        const std::filesystem::path temp_filepath =
            dst.parent_path() / (dst.filename().string() + ".partial-" + unique_suffix());
        try {
            copy(src, temp_filepath);
            std::filesystem::permissions(
                temp_filepath,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::add);
            // Windows refuses to replace a read-only file (a copy left by an
            // older version); POSIX does not care. Making dst writable first
            // opens no window in which it is missing or partial.
            std::error_code ec;
            if (std::filesystem::exists(dst, ec)) {
                std::filesystem::permissions(dst, std::filesystem::perms::owner_write,
                                             std::filesystem::perm_options::add, ec);
            }
            std::filesystem::rename(temp_filepath, dst);
        } catch (...) {
            std::error_code ec;
            std::filesystem::remove(temp_filepath, ec);
            throw;
        }
    }

private:
    static void copy_master_to_working(const std::filesystem::path& src,
                                       const std::filesystem::path& dst) {
        copy_file_atomically(src, dst);
    }

    static void copy_file_plain(const std::filesystem::path& src,
                                const std::filesystem::path& dst) {
        std::filesystem::copy_file(src, dst);
    }

    // Distinguishes the temporaries of concurrent copies to the same
    // destination, in this process or another.
    static std::string unique_suffix() {
        std::random_device rd;
        return std::to_string((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
    }
};

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_DISC_PATHS_HPP

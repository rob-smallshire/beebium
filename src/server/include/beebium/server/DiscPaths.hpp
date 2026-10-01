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

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

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
// Reference forms (mirrors RomPaths' filename rules):
//   * absolute path, or relative path WITH a directory component
//       -> an explicit user-supplied image: returned as-is, opened writable,
//          no copy-on-write. Exactly today's behaviour.
//   * bare filename (no directory component)
//       -> a bundled master reference: resolved to a working copy (below).
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
    // Throws if none exists (a bare-name reference cannot be resolved without
    // it).
    static std::filesystem::path find_disc_directory() {
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

        throw std::runtime_error(
            "Cannot find disc directory. Set BEEBIUM_DISC_DIR environment "
            "variable to the directory containing the bundled disc images.");
    }

    // Per-user working-copy directory: a sibling of the user presets
    // directory (see PresetPaths), so all Beebium per-user state lives
    // together. Not created here; prepare_working_image() creates it.
    static std::filesystem::path get_user_disc_working_dirpath() {
        return user_state_base() / "discs";
    }

    // Resolve a scsi-hdd "image" reference to the path the emulator should
    // open. Reads BEEBIUM_DISC_WORK_DIR to pick Scratch vs Persistent, and
    // uses find_disc_directory() as the master source. See the
    // dir-injecting overload for the actual policy.
    static std::filesystem::path resolve_disc_image(std::string_view image) {
        std::filesystem::path ref(image);
        // Explicit user path (absolute or with a directory component): as-is.
        if (ref.is_absolute() || ref.has_parent_path()) {
            return ref;
        }

        std::optional<std::filesystem::path> work_override;
        if (auto env = beebium::platform::get_env("BEEBIUM_DISC_WORK_DIR")) {
            work_override = std::filesystem::path(*env);
        }
        const DiscWorkMode mode =
            work_override ? DiscWorkMode::Scratch : DiscWorkMode::Persistent;
        const std::filesystem::path work_dir =
            work_override ? *work_override : get_user_disc_working_dirpath();

        return prepare_working_image(image, find_disc_directory(), work_dir, mode);
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
            std::filesystem::path(dat_name).replace_extension(".dsc");

        const auto master_dat = master_dir / dat_name;
        const auto master_dsc = master_dir / dsc_name;
        if (!std::filesystem::exists(master_dat) ||
            !std::filesystem::exists(master_dsc)) {
            throw std::runtime_error(
                "Bundled disc image not found: " + master_dat.string() +
                " (with its .dsc sidecar)");
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

    // Base directory for per-user Beebium state (the parent of the user
    // presets "presets" dir; see PresetPaths, kept deliberately in step).
    // Delegates to the shared platform helper so all three users -- PresetPaths,
    // DiscPaths and the AUN transport -- agree without copying the logic.
    static std::filesystem::path user_state_base() {
        return beebium::platform::user_state_base_dirpath();
    }
};

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_DISC_PATHS_HPP

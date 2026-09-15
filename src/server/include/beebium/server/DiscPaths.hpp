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

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
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

        auto exe_dirpath = get_executable_directory();

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
    //   * Scratch: always replace the working copy with a fresh master copy
    //     (a stale prior copy is removed first, so the replace truncates
    //     rather than reuses).
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

private:
    // Remove any stale destination (which may be read-only from a prior
    // scratch copy) then copy fresh and make the copy writable, so the
    // emulator can open it read/write even though the master is read-only.
    static void copy_master_to_working(const std::filesystem::path& src,
                                       const std::filesystem::path& dst) {
        std::error_code ec;
        std::filesystem::remove(dst, ec);  // ignore "didn't exist"
        std::filesystem::copy_file(
            src, dst, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(
            dst,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::add);
    }

    // Base directory for per-user Beebium state (the parent of the user
    // presets "presets" dir; see PresetPaths, kept deliberately in step).
    static std::filesystem::path user_state_base() {
#ifdef __APPLE__
        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / "Library" / "Application Support" / "Beebium";
        }
#elif defined(_WIN32)
        if (auto appdata = beebium::platform::get_env("APPDATA")) {
            return std::filesystem::path(*appdata) / "Beebium";
        }
#else
        if (auto xdg_config = beebium::platform::get_env("XDG_CONFIG_HOME")) {
            return std::filesystem::path(*xdg_config) / "beebium";
        }
        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / ".config" / "beebium";
        }
        if (struct passwd* pw = getpwuid(getuid())) {
            return std::filesystem::path(pw->pw_dir) / ".config" / "beebium";
        }
#endif
        return std::filesystem::current_path() / "beebium";
    }

    static std::filesystem::path get_executable_directory() {
#ifdef __APPLE__
        char path[PATH_MAX];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0) {
            return std::filesystem::path(path).parent_path();
        }
#elif defined(_WIN32)
        char path[MAX_PATH];
        if (GetModuleFileNameA(nullptr, path, MAX_PATH) > 0) {
            return std::filesystem::path(path).parent_path();
        }
#else
        char path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (len != -1) {
            path[len] = '\0';
            return std::filesystem::path(path).parent_path();
        }
#endif
        return std::filesystem::current_path();
    }
};

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_DISC_PATHS_HPP

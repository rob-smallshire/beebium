// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVER_ROM_PATHS_HPP
#define BEEBIUM_SERVER_ROM_PATHS_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>
#include <beebium/PlatformUtils.hpp>
#include <beebium/server/ResourcePaths.hpp>

namespace beebium::server {

// ROM path discovery utility.
//
// Search order for ROM directory:
// 1. Explicit path from set_rom_directory() (if set)
// 2. BEEBIUM_ROM_DIR environment variable
// 3. a roms/ directory at or above the executable (build directory layout)
// 4. ../share/beebium/roms/ relative to executable (installed layout)
// 5. Compile-time BEEBIUM_DEFAULT_ROM_DIR (fallback for development)
//
// An individual ROM is found by the file reference rule (ResourcePaths.hpp),
// with the ROM directory as the system directory for ROMs.

class RomPaths {
public:
    // Set explicit ROM directory (takes priority over all other search methods)
    static void set_rom_directory(const std::filesystem::path& dirpath) {
        explicit_rom_dirpath_ = dirpath;
    }

    // Clear explicit ROM directory
    static void clear_rom_directory() {
        explicit_rom_dirpath_.reset();
    }

    // Find the ROM directory using the search order; nullopt when there is
    // none. An explicit directory that does not exist is an error.
    static std::optional<std::filesystem::path> find_rom_directory() {
        // 1. Explicit path takes priority
        if (explicit_rom_dirpath_) {
            if (std::filesystem::is_directory(*explicit_rom_dirpath_)) {
                return *explicit_rom_dirpath_;
            }
            throw std::runtime_error(
                "Explicit ROM directory does not exist: " + explicit_rom_dirpath_->string());
        }

        // 2. Environment variable
        if (auto env_dir = get_env("BEEBIUM_ROM_DIR")) {
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

        // 3. Build directory layout: a roms/ directory at or above the
        //    executable's directory. We walk upward (bounded) rather than
        //    assuming a fixed depth so the lookup still works under multi-config
        //    generators (e.g. Visual Studio), which nest the executable in a
        //    per-config subdirectory such as ".../Release/".
        {
            auto dir = exe_dirpath;
            for (int level = 0; level < 6; ++level) {
                auto build_roms = dir / "roms";
                if (std::filesystem::is_directory(build_roms)) {
                    return build_roms;
                }
                auto parent = dir.parent_path();
                if (parent == dir) {
                    break;  // reached the filesystem root
                }
                dir = parent;
            }
        }

        // 4. Installed layout: ../share/beebium/roms/ relative to executable
        auto installed_roms = exe_dirpath.parent_path() / "share" / "beebium" / "roms";
        if (std::filesystem::is_directory(installed_roms)) {
            return installed_roms;
        }

        // 5. Compile-time fallback (if defined)
#ifdef BEEBIUM_DEFAULT_ROM_DIR
        std::filesystem::path default_path(BEEBIUM_DEFAULT_ROM_DIR);
        if (std::filesystem::is_directory(default_path)) {
            return default_path;
        }
#endif

        return std::nullopt;
    }

    // The ROM file a reference names, written in `container_dirpath` (see
    // ResourcePaths.hpp). Throws when it does not exist, naming where it was
    // looked for.
    static std::filesystem::path find_rom(std::string_view reference,
                                          const std::filesystem::path& container_dirpath) {
        std::vector<std::filesystem::path> system_dirpaths;
        if (auto rom_dirpath = find_rom_directory()) {
            system_dirpaths.push_back(*rom_dirpath);
        }
        std::filesystem::path filepath;
        try {
            filepath = resolve_reference(std::string(reference), ResourceKind::Rom,
                                         container_dirpath, system_dirpaths);
        } catch (const std::runtime_error& e) {
            if (!system_dirpaths.empty()) throw;
            throw std::runtime_error(std::string(e.what()) +
                                     " (no ROM directory found: set BEEBIUM_ROM_DIR "
                                     "or use --rom-dir)");
        }
        if (!std::filesystem::exists(filepath)) {
            throw std::runtime_error("ROM file not found: " + filepath.string());
        }
        return filepath;
    }

private:
    static inline std::optional<std::filesystem::path> explicit_rom_dirpath_;

    static std::optional<std::string> get_env(const char* name) {
        return beebium::platform::get_env(name);
    }
};

} // namespace beebium::server

#endif // BEEBIUM_SERVER_ROM_PATHS_HPP

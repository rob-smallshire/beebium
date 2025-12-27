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
#include <cstdlib>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

namespace beebium::server {

// ROM path discovery utility.
//
// Search order for ROM directory:
// 1. Explicit path from set_rom_directory() (if set)
// 2. BEEBIUM_ROM_DIR environment variable
// 3. ../roms/ relative to executable (build directory layout)
// 4. ../share/beebium/roms/ relative to executable (installed layout)
// 5. Compile-time BEEBIUM_DEFAULT_ROM_DIR (fallback for development)
//
// For individual ROM files:
// 1. If absolute path → return as-is
// 2. If relative path with directory component → resolve against cwd
// 3. Otherwise → look in rom_directory/filename

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

    // Find the ROM directory using the search order
    static std::filesystem::path find_rom_directory() {
        // 1. Explicit path takes priority
        if (explicit_rom_dirpath_) {
            if (std::filesystem::is_directory(*explicit_rom_dirpath_)) {
                return *explicit_rom_dirpath_;
            }
            throw std::runtime_error(
                "Explicit ROM directory does not exist: " + explicit_rom_dirpath_->string());
        }

        // 2. Environment variable
        if (const char* env_dir = std::getenv("BEEBIUM_ROM_DIR")) {
            std::filesystem::path env_path(env_dir);
            if (std::filesystem::is_directory(env_path)) {
                return env_path;
            }
        }

        // Get executable directory for relative paths
        auto exe_dirpath = get_executable_directory();

        // 3. Build directory layout: ../roms/ relative to executable
        auto build_roms = exe_dirpath.parent_path().parent_path() / "roms";
        if (std::filesystem::is_directory(build_roms)) {
            return build_roms;
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

        throw std::runtime_error(
            "Cannot find ROM directory. Set BEEBIUM_ROM_DIR environment variable "
            "or use --rom-dir option.");
    }

    // Find a specific ROM file
    // If filepath is absolute, returns it as-is (after checking existence)
    // If filepath contains directory separators, resolves against cwd
    // Otherwise, looks in the ROM directory
    static std::filesystem::path find_rom(std::string_view filename) {
        std::filesystem::path filepath(filename);

        // Absolute path - return as-is
        if (filepath.is_absolute()) {
            if (!std::filesystem::exists(filepath)) {
                throw std::runtime_error("ROM file not found: " + std::string(filename));
            }
            return filepath;
        }

        // Relative path with directory component - resolve against cwd
        if (filepath.has_parent_path()) {
            auto resolved = std::filesystem::current_path() / filepath;
            if (!std::filesystem::exists(resolved)) {
                throw std::runtime_error("ROM file not found: " + resolved.string());
            }
            return resolved;
        }

        // Simple filename - look in ROM directory
        auto rom_dirpath = find_rom_directory();
        auto rom_filepath = rom_dirpath / filepath;
        if (!std::filesystem::exists(rom_filepath)) {
            throw std::runtime_error(
                "ROM file not found: " + rom_filepath.string() +
                " (searched in " + rom_dirpath.string() + ")");
        }
        return rom_filepath;
    }

private:
    static inline std::optional<std::filesystem::path> explicit_rom_dirpath_;

    // Get the directory containing the executable
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
        // Fallback to current directory
        return std::filesystem::current_path();
    }
};

} // namespace beebium::server

#endif // BEEBIUM_SERVER_ROM_PATHS_HPP

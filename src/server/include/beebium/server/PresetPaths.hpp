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

#ifndef BEEBIUM_SERVER_PRESET_PATHS_HPP
#define BEEBIUM_SERVER_PRESET_PATHS_HPP

#include "beebium/server/Platform.hpp"

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
// The per-user presets directory comes from the APPDATA environment variable
// (via platform::get_env) rather than SHGetFolderPathA, avoiding the shell API
// headers.
#elif !defined(__APPLE__)
#include <unistd.h>
#include <pwd.h>
#endif

namespace beebium::server {

// Preset path discovery utility.
//
// System presets (shipped with software):
//   1. BEEBIUM_SERVERS_DIRPATH environment variable + /presets/
//   2. ../presets/ relative to executable (build directory layout)
//   3. ../share/beebium/presets/ relative to executable (installed layout)
//
// User presets (created by users):
//   1. BEEBIUM_USER_PRESETS_DIRPATH environment variable
//   2. Platform-specific application data directory:
//      - macOS:   ~/Library/Application Support/Beebium/presets/
//      - Linux:   ~/.config/beebium/presets/
//      - Windows: %APPDATA%/Beebium/presets/

class PresetPaths {
public:
    // Preset file extension
    static constexpr std::string_view PRESET_EXTENSION = ".preset.beebium";

    // Get the system presets directory (shipped with software)
    // Returns empty optional if no system presets directory found
    static std::optional<std::filesystem::path> get_system_presets_dirpath() {
        // 1. Environment variable override
        if (auto env_dir = platform::get_env("BEEBIUM_SERVERS_DIRPATH")) {
            std::filesystem::path presets_path = std::filesystem::path(*env_dir) / "presets";
            if (std::filesystem::is_directory(presets_path)) {
                return presets_path;
            }
        }

        // Anchor the relative lookups on the executable's real on-disk
        // directory (symlinks resolved, so a bin/ symlink into an installed
        // tree still finds that tree's share/). Fall back to the current
        // directory when the OS cannot report the executable's location.
        auto exe_dirpath = beebium::platform::executable_directory()
                               .value_or(std::filesystem::current_path());

        // 2. Build directory layout: sibling presets/ directory
        auto build_presets = exe_dirpath / "presets";
        if (std::filesystem::is_directory(build_presets)) {
            return build_presets;
        }

        // 3. Installed layout: ../share/beebium/presets/ relative to executable
        auto installed_presets = exe_dirpath.parent_path() / "share" / "beebium" / "presets";
        if (std::filesystem::is_directory(installed_presets)) {
            return installed_presets;
        }

        return std::nullopt;
    }

    // Get the user presets directory (user-created presets)
    // This directory may not exist yet; use ensure_user_presets_dirpath_exists() to create it
    static std::filesystem::path get_user_presets_dirpath() {
        // 1. Environment variable override
        if (auto env_dir = platform::get_env("BEEBIUM_USER_PRESETS_DIRPATH")) {
            return std::filesystem::path(*env_dir);
        }

        // 2. Platform-specific application data directory
        return get_platform_user_presets_dirpath();
    }

    // Ensure the user presets directory exists, creating it if necessary
    // Returns true if directory exists or was created successfully
    static bool ensure_user_presets_dirpath_exists() {
        auto dirpath = get_user_presets_dirpath();
        if (std::filesystem::is_directory(dirpath)) {
            return true;
        }
        std::error_code ec;
        return std::filesystem::create_directories(dirpath, ec);
    }

    // Find a preset file by ID
    // Searches system presets first, then user presets
    // Returns full path if found, nullopt otherwise
    static std::optional<std::filesystem::path> find_preset_filepath(
        const std::string& preset_id,
        bool system_only = false,
        bool user_only = false
    ) {
        std::string filename = preset_id + std::string(PRESET_EXTENSION);

        // Search system presets first (unless user_only)
        if (!user_only) {
            if (auto system_dir = get_system_presets_dirpath()) {
                auto filepath = *system_dir / filename;
                if (std::filesystem::exists(filepath)) {
                    return filepath;
                }
            }
        }

        // Then search user presets (unless system_only)
        if (!system_only) {
            auto user_dir = get_user_presets_dirpath();
            auto filepath = user_dir / filename;
            if (std::filesystem::exists(filepath)) {
                return filepath;
            }
        }

        return std::nullopt;
    }

    // Check if a preset file is a system preset (read-only)
    static bool is_system_preset(const std::filesystem::path& filepath) {
        auto system_dir = get_system_presets_dirpath();
        if (!system_dir) {
            return false;
        }

        // Normalize paths for comparison
        std::error_code ec;
        auto canonical_preset = std::filesystem::weakly_canonical(filepath, ec);
        if (ec) return false;

        auto canonical_system = std::filesystem::weakly_canonical(*system_dir, ec);
        if (ec) return false;

        // Check if preset is within system directory
        auto rel = canonical_preset.lexically_relative(canonical_system);
        return !rel.empty() && rel.native()[0] != '.';
    }

    // List all preset files in a directory
    // Returns list of preset IDs (filenames without extension)
    static std::vector<std::string> list_presets_in_directory(
        const std::filesystem::path& dirpath
    ) {
        std::vector<std::string> preset_ids;

        if (!std::filesystem::is_directory(dirpath)) {
            return preset_ids;
        }

        for (const auto& entry : std::filesystem::directory_iterator(dirpath)) {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (filename.size() > PRESET_EXTENSION.size() &&
                filename.substr(filename.size() - PRESET_EXTENSION.size()) == PRESET_EXTENSION) {
                // Extract preset ID (filename without extension)
                std::string preset_id = filename.substr(0, filename.size() - PRESET_EXTENSION.size());
                preset_ids.push_back(preset_id);
            }
        }

        return preset_ids;
    }

    // Slugify a display name to create a preset ID
    // "BBC Model B with Acorn DFS" → "bbc-model-b-with-acorn-dfs"
    // Rules:
    // - Convert to lowercase
    // - Replace spaces and underscores with hyphens
    // - Remove non-alphanumeric characters (except hyphens)
    // - Collapse multiple hyphens
    // - Trim leading/trailing hyphens
    static std::string slugify(const std::string& name) {
        std::string result;
        result.reserve(name.size());

        bool last_was_hyphen = true;  // Treat start as hyphen to trim leading

        for (char c : name) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                last_was_hyphen = false;
            } else if (c == ' ' || c == '_' || c == '-') {
                if (!last_was_hyphen) {
                    result += '-';
                    last_was_hyphen = true;
                }
            }
            // Skip other characters
        }

        // Trim trailing hyphen
        while (!result.empty() && result.back() == '-') {
            result.pop_back();
        }

        return result;
    }

    // Generate a unique preset ID based on a base ID
    // If base_id exists, appends -2, -3, etc. until unique
    static std::string generate_unique_preset_id(const std::string& base_id) {
        // Check if base ID is available
        if (!find_preset_filepath(base_id, false, true)) {
            return base_id;
        }

        // Try numbered suffixes
        for (int i = 2; i < 1000; ++i) {
            std::string candidate = base_id + "-" + std::to_string(i);
            if (!find_preset_filepath(candidate, false, true)) {
                return candidate;
            }
        }

        // Fallback (should never happen)
        return base_id + "-new";
    }

private:
    // Get platform-specific user presets directory
    static std::filesystem::path get_platform_user_presets_dirpath() {
#ifdef __APPLE__
        // macOS: ~/Library/Application Support/Beebium/presets/
        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / "Library" / "Application Support" / "Beebium" / "presets";
        }
#elif defined(_WIN32)
        // Windows: %APPDATA%/Beebium/presets/
        if (auto appdata = platform::get_env("APPDATA")) {
            return std::filesystem::path(*appdata) / "Beebium" / "presets";
        }
#else
        // Linux/Unix: ~/.config/beebium/presets/
        // Respect XDG_CONFIG_HOME if set
        if (auto xdg_config = platform::get_env("XDG_CONFIG_HOME")) {
            return std::filesystem::path(*xdg_config) / "beebium" / "presets";
        }
        if (const char* home = std::getenv("HOME")) {
            return std::filesystem::path(home) / ".config" / "beebium" / "presets";
        }
        // Last resort: use passwd entry
        if (struct passwd* pw = getpwuid(getuid())) {
            return std::filesystem::path(pw->pw_dir) / ".config" / "beebium" / "presets";
        }
#endif
        // Ultimate fallback: current directory
        return std::filesystem::current_path() / "beebium_presets";
    }
};

} // namespace beebium::server

#endif // BEEBIUM_SERVER_PRESET_PATHS_HPP

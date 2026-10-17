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
#include "beebium/server/PresetSource.hpp"
#include "beebium/server/PresetWriter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <filesystem>
#include <optional>
#include <set>
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
// User presets (created by users): presets/ in the per-user directory
// (platform::user_state_base_dirpath, which BEEBIUM_USER_DIRPATH overrides):
//      - macOS:   ~/Library/Application Support/Beebium/presets/
//      - Linux:   ~/.config/beebium/presets/
//      - Windows: %APPDATA%/Beebium/presets/

class PresetPaths {
public:
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
        return beebium::platform::user_state_base_dirpath() / "presets";
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

    // Where a stored preset lives: shipped with the software, or the user's.
    enum class Origin { System, User };

    // A stored preset: its id, its file and which store holds it.
    struct Entry {
        std::string id;
        std::filesystem::path filepath;
        Origin origin;
    };

    // The file name a stored preset with `id` has.
    static std::string filename_for(std::string_view id) {
        return std::string(id) + std::string(PRESET_EXTENSION);
    }

    // The id of the stored preset named `filename`, or nullopt when
    // `filename` is not a stored preset's name.
    static std::optional<std::string> id_from_filename(const std::filesystem::path& filename) {
        return without_suffix(filename.filename().string(), PRESET_EXTENSION);
    }

    // The name a preset at `path` is called by, to make an id from: a stored
    // preset's id, the name of a file in the earlier stored form without its
    // suffix, or else the name of a directory or the stem of any other file.
    static std::string name_of(const std::filesystem::path& path) {
        const auto named = path.has_filename() ? path : path.parent_path();
        const auto filename = named.filename().string();
        if (auto id = without_suffix(filename, PRESET_EXTENSION)) return *id;
        if (auto id = without_suffix(filename, LEGACY_PRESET_EXTENSION)) return *id;
        std::error_code ec;
        return std::filesystem::is_directory(named, ec) ? filename : named.stem().string();
    }

    // Every stored preset, in search order: the system presets, then the
    // user presets, each by id. The user store's presets in the earlier
    // stored form are first made archives (migrate_user_presets).
    static std::vector<Entry> enumerate() {
        std::vector<Entry> entries;
        if (auto system_dirpath = get_system_presets_dirpath()) {
            append_entries(*system_dirpath, Origin::System, entries);
        }
        migrate_user_presets(get_user_presets_dirpath());
        append_entries(get_user_presets_dirpath(), Origin::User, entries);
        return entries;
    }

    // Makes each preset in `dirpath` stored in the earlier form -- a lone
    // <id>.preset.beebium, with its thumbnail beside it as <id>.thumbnail.png
    // -- an archive, <id>.beebium-preset, with one line on stderr for each.
    // When the store cannot be written it is left as it is, with a one-line
    // notice, and the old files are ignored. Run on the user store only, once
    // a process for each store. One-shot: removed after the release that
    // ships it.
    static void migrate_user_presets(const std::filesystem::path& dirpath) {
        static std::set<std::filesystem::path> migrated_dirpaths;
        if (!migrated_dirpaths.insert(dirpath).second) return;
        std::error_code ec;
        std::vector<std::filesystem::path> legacy_filepaths;
        for (const auto& entry : std::filesystem::directory_iterator(dirpath, ec)) {
            if (entry.is_regular_file(ec) &&
                without_suffix(entry.path().filename().string(), LEGACY_PRESET_EXTENSION)) {
                legacy_filepaths.push_back(entry.path());
            }
        }
        std::sort(legacy_filepaths.begin(), legacy_filepaths.end());
        for (const auto& legacy_filepath : legacy_filepaths) {
            const auto id =
                without_suffix(legacy_filepath.filename().string(), LEGACY_PRESET_EXTENSION);
            const auto target_filepath = dirpath / filename_for(*id);
            if (std::filesystem::exists(target_filepath, ec)) {
                std::cerr << "Not migrating the preset " << legacy_filepath.string() << ": "
                          << target_filepath.string() << " already exists\n";
                continue;
            }
            std::ifstream in(legacy_filepath, std::ios::binary);
            const std::string document((std::istreambuf_iterator<char>(in)),
                                       std::istreambuf_iterator<char>());
            auto members = preset_members(document);
            const auto thumbnail_filepath = dirpath / (*id + LEGACY_THUMBNAIL_SUFFIX);
            if (std::filesystem::is_regular_file(thumbnail_filepath, ec)) {
                std::ifstream thumbnail(thumbnail_filepath, std::ios::binary);
                members.push_back({PresetSource::THUMBNAIL_FILENAME,
                                   std::vector<std::uint8_t>(
                                       std::istreambuf_iterator<char>(thumbnail), {})});
            }
            if (auto error = write_preset(std::move(members), target_filepath); !error.empty()) {
                std::cerr << "Cannot migrate the presets in " << dirpath.string() << " to "
                          << PRESET_EXTENSION << " archives (" << error
                          << "); those stored as " << LEGACY_PRESET_EXTENSION
                          << " are ignored\n";
                return;
            }
            std::filesystem::remove(legacy_filepath, ec);
            std::filesystem::remove(thumbnail_filepath, ec);
            std::cerr << "Migrated the preset " << legacy_filepath.string() << " to "
                      << target_filepath.string() << "\n";
        }
    }

    // The stored preset with `id`, the first in search order, or only from
    // the store `origin` when it is given.
    static std::optional<Entry> find(const std::string& id,
                                     std::optional<Origin> origin = std::nullopt) {
        for (auto& entry : enumerate()) {
            if (entry.id == id && (!origin || entry.origin == *origin)) return std::move(entry);
        }
        return std::nullopt;
    }

    // The preset an argument names: a path to a preset (a file, or a
    // directory holding one; see PresetSource) or, failing that, the id of a
    // stored preset, found in search order. Every subcommand taking a preset
    // by path or id locates it here.
    static std::optional<std::filesystem::path> locate(const std::string& preset_arg) {
        std::error_code ec;
        if (std::filesystem::exists(preset_arg, ec)) {
            return std::filesystem::path(preset_arg);
        }
        if (auto entry = find(preset_arg)) return entry->filepath;
        return std::nullopt;
    }

    // What to say when locate() finds nothing.
    static std::string not_found_message(const std::string& preset_arg) {
        return "Preset not found: '" + preset_arg +
               "' (no such file, and no system or user preset with that id)";
    }

    // The preset an argument names (see locate), opened; or nullopt with
    // `error` saying why there is none.
    static std::optional<PresetSource> open(const std::string& preset_arg, std::string& error) {
        auto path = locate(preset_arg);
        if (!path) {
            error = not_found_message(preset_arg);
            return std::nullopt;
        }
        return PresetSource::open(*path, error);
    }

    // A preset's thumbnail, when it has one: thumbnail.png in a directory or
    // archive preset. A lone preset file has none.
    static std::optional<std::filesystem::path> find_thumbnail(const PresetSource& source) {
        if (source.form() == PresetSource::Form::File) return std::nullopt;
        auto filepath = source.container().dirpath() / PresetSource::THUMBNAIL_FILENAME;
        if (source.container().has_file(filepath)) return filepath;
        return std::nullopt;
    }

    // Where a user preset with `id` is stored.
    static std::filesystem::path user_filepath_for(std::string_view id) {
        return get_user_presets_dirpath() / filename_for(id);
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
    // If a preset in either store has base_id, appends -2, -3, etc. until
    // unique, so a user preset is never hidden behind a system one
    static std::string generate_unique_preset_id(const std::string& base_id) {
        // Check if base ID is available
        if (!find(base_id)) {
            return base_id;
        }

        // Try numbered suffixes
        for (int i = 2; i < 1000; ++i) {
            std::string candidate = base_id + "-" + std::to_string(i);
            if (!find(candidate)) {
                return candidate;
            }
        }

        // Fallback (should never happen)
        return base_id + "-new";
    }

private:
    // The suffix of a stored preset's file name: the one place it is named.
    static constexpr std::string_view PRESET_EXTENSION = ".beebium-preset";
    // The earlier stored form, a lone document with its thumbnail beside it,
    // named for migrate_user_presets and name_of only.
    static constexpr std::string_view LEGACY_PRESET_EXTENSION = ".preset.beebium";
    static constexpr const char* LEGACY_THUMBNAIL_SUFFIX = ".thumbnail.png";

    // `name` without `suffix`, or nullopt when it does not end with it or is
    // nothing more.
    static std::optional<std::string> without_suffix(const std::string& name,
                                                     std::string_view suffix) {
        if (name.size() <= suffix.size() ||
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            return std::nullopt;
        }
        return name.substr(0, name.size() - suffix.size());
    }

    // The stored presets directly in `dirpath`, by id.
    static void append_entries(const std::filesystem::path& dirpath, Origin origin,
                               std::vector<Entry>& entries) {
        std::vector<Entry> found;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dirpath, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (auto id = id_from_filename(entry.path())) {
                found.push_back({std::move(*id), entry.path(), origin});
            }
        }
        std::sort(found.begin(), found.end(),
                  [](const Entry& a, const Entry& b) { return a.id < b.id; });
        entries.insert(entries.end(), std::make_move_iterator(found.begin()),
                       std::make_move_iterator(found.end()));
    }
};

} // namespace beebium::server

#endif // BEEBIUM_SERVER_PRESET_PATHS_HPP

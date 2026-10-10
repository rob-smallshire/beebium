// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_EXTENSION_PLUGIN_LOADER_HPP
#define BEEBIUM_EXTENSION_PLUGIN_LOADER_HPP

#include "Export.hpp"
#include "ConfigValidation.hpp"
#include "Extension.hpp"
#include "ExtensionManifest.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace beebium {

// Scans an extension directory for manifest.json files, and loads
// requested extensions by name via dlopen/LoadLibrary.
//
// Plugin loading is opt-in: scan_manifests() enumerates available
// extensions without loading any code; load_extension() loads a
// specific extension by manifest.
class BEEBIUM_EXT_API PluginLoader {
public:
    ~PluginLoader();

    // Non-copyable, non-movable (owns dlopen handles)
    PluginLoader() = default;
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Behaviour when scan_manifests is asked to scan a directory that
    // does not exist. Use Throw for paths the user explicitly named
    // (so a typo or stale path is reported, not silently ignored), and
    // ReturnEmpty for opportunistic scans of conventional locations
    // that may legitimately be absent.
    enum class MissingDirPolicy {
        ReturnEmpty,
        Throw,
    };

    // Scan directory for manifest.json files. No code is loaded.
    // Each subdirectory or direct manifest.json in the directory is checked.
    std::vector<ExtensionManifest> scan_manifests(
        const std::filesystem::path& extension_dirpath,
        MissingDirPolicy on_missing = MissingDirPolicy::ReturnEmpty);

    // Load a specific extension from its manifest. The shared library
    // named in the manifest is loaded and the entry point called.
    // Returns the extension instance (manifest and config already set).
    // The caller is responsible for downcasting to the extension-point
    // type matching manifest.extension_kind and registering it with the
    // appropriate registry.
    std::unique_ptr<Extension> load_extension(
        const ExtensionManifest& manifest,
        std::map<std::string, std::string> config = {},
        std::map<std::string, std::vector<std::string>> list_config = {});

    // Ask the extension's plugin to check an instance configuration, already
    // normalised against the manifest, through the configuration check it
    // exports (kValidateConfigSymbol). Loads the plugin's library, as
    // load_extension does, but creates no extension. Returns what the plugin
    // found; empty when it exports no check. Throws std::runtime_error when
    // the library cannot be loaded or was built for another extension ABI.
    std::vector<ConfigDiagnostic> validate_config(
        const ExtensionManifest& manifest,
        const std::map<std::string, std::string>& config,
        const std::map<std::string, std::vector<std::string>>& list_config);

    // Find a manifest by name from a previously scanned list.
    static const ExtensionManifest* find_manifest(
        const std::vector<ExtensionManifest>& manifests,
        std::string_view name);

private:
    // Load the manifest's library, refusing one of another extension ABI, or
    // return the handle of the one already loaded. Throws std::runtime_error.
    void* open_library(const ExtensionManifest& manifest);

    struct LoadedPlugin {
        void* handle = nullptr;
        std::string name;
        std::filesystem::path library_filepath;
    };
    std::vector<LoadedPlugin> loaded_plugins_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_PLUGIN_LOADER_HPP

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

#ifndef BEEBIUM_EXTENSION_EXTENSION_HPP
#define BEEBIUM_EXTENSION_EXTENSION_HPP

#include "Export.hpp"
#include "ExtensionManifest.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Forward decl + helper declaration; defined in Extension.cpp.

namespace beebium {

class ExtensionUi;             // forward decl; defined in ExtensionUi.hpp
class ExtensionStorage;        // forward decl; defined in ExtensionStorage.hpp
class ExtensionRpcDispatcher;  // forward decl; defined in ExtensionRpc.hpp

// Common base for all extension-point types. Holds manifest, instance
// config, and identity accessors. No lifecycle methods -- those belong
// on the derived extension-point classes (PeripheralExtension,
// EconetTransportExtension, etc.) because each kind of extension has
// its own activation contract with the host machine.
//
// The manifest is the single source of truth for extension metadata
// (name, description, parameter schema, extension kind). For
// dynamically loaded extensions it is read from manifest.json; for
// built-in extensions it is constructed programmatically.
//
// Instance config (the parameters passed via CLI or preset) is stored
// as a string -> string map. Extensions parse typed values out of it
// during their own initialisation.
class BEEBIUM_EXT_TYPE_VISIBLE Extension {
public:
    // See ExtensionUi.hpp for the BEEBIUM_EXT_TYPE_VISIBLE / per-method
    // BEEBIUM_EXT_API split. Inline accessors below stay genuinely
    // inline in consumer DLLs that don't link beebium_extension_api.
    BEEBIUM_EXT_API virtual ~Extension();

    // Set the manifest (called by the framework before init).
    void set_manifest(ExtensionManifest manifest) { manifest_ = std::move(manifest); }

    // Access the manifest.
    const ExtensionManifest& manifest() const { return manifest_; }

    // Set instance configuration (called by the framework before init).
    // Config is parsed from CLI arguments or preset files. Scalar params
    // go here; list params (is_list=true in the schema) go into
    // list_config via set_list_config.
    void set_config(std::map<std::string, std::string> config) { config_ = std::move(config); }

    // Set or replace a single config entry. Intended for framework use
    // (the registries call this to auto-assign a default `id` when the
    // user didn't supply one); extensions themselves should configure
    // via set_config() with the full map.
    void set_config_value(std::string key, std::string value) {
        config_[std::move(key)] = std::move(value);
    }

    // Set list-valued instance configuration (is_list params). Called by
    // the framework before init, alongside set_config.
    void set_list_config(std::map<std::string, std::vector<std::string>> list_config) {
        list_config_ = std::move(list_config);
    }

    // Access a single config value by key.
    std::optional<std::string_view> config_value(std::string_view key) const {
        auto it = config_.find(std::string(key));
        if (it != config_.end()) {
            return std::string_view(it->second);
        }
        return std::nullopt;
    }

    // Typed accessor for a "boolean" manifest parameter. Accepts "true"/"1"
    // as true and "false"/"0" as false (the set parse_extension_args
    // validates). Returns `fallback` when the key is absent, empty, or holds
    // an unrecognised value.
    bool config_bool(std::string_view key, bool fallback = false) const {
        auto value = config_value(key);
        if (!value || value->empty()) {
            return fallback;
        }
        if (*value == "true" || *value == "1") {
            return true;
        }
        if (*value == "false" || *value == "0") {
            return false;
        }
        return fallback;
    }

    // Access a list-valued config parameter. Returns nullopt if the key
    // is absent. The returned span aliases into this extension's owned
    // storage and is valid for the lifetime of the Extension.
    std::optional<std::span<const std::string>> config_list(std::string_view key) const {
        auto it = list_config_.find(std::string(key));
        if (it != list_config_.end()) {
            return std::span<const std::string>(it->second);
        }
        return std::nullopt;
    }

    // --- Packaged ROM images (manifest `roms`) ---
    //
    // A plugin ships its firmware in its own directory and declares each image
    // in the manifest; these resolve and load it. Coprocessor firmware belongs
    // to the coprocessor, not the shared host ROM set, and no extension needs a
    // server header to find its own files.

    // Resolve a declared ROM (by key) to its path beside the manifest:
    // <manifest_dirpath>/roms/<filename>. Throws std::runtime_error naming the
    // key if it is not declared. Does not consult config; an explicit override
    // is the extension's own concern.
    BEEBIUM_EXT_API std::filesystem::path rom_filepath(std::string_view key) const;

    // Load the ROM declared under `key` into `dest`, whose size must equal the
    // declared size. Accepts either an exact-size image or a double-size image
    // whose lower half is all 0xFF (an EPROM dump padded to the next size),
    // using the upper half; logs which form it found. Throws std::runtime_error
    // naming the resolved path for a missing file, any other size, or a
    // double-size file whose lower half is not blank.
    BEEBIUM_EXT_API void load_rom(std::string_view key, std::span<std::uint8_t> dest) const;

    // Access all config values.
    const std::map<std::string, std::string>& config() const { return config_; }

    // Access all list-valued config entries.
    const std::map<std::string, std::vector<std::string>>& list_config() const { return list_config_; }

    // Instance ID (from config["id"] or empty if not yet assigned).
    std::string_view id() const {
        auto it = config_.find("id");
        return (it != config_.end()) ? std::string_view(it->second) : std::string_view{};
    }

    // Display label for this instance.
    //
    // Resolution order:
    //   1. config["label"] -- user override (CLI: `--ext label="Foo"`)
    //   2. default_label() -- subclass hook for type-specific defaults
    //
    // This is non-virtual so the "explicit label wins" rule cannot be
    // accidentally broken by an override; subclasses customise via
    // default_label() instead.
    std::string label() const {
        auto it = config_.find("label");
        if (it != config_.end() && !it->second.empty()) {
            return it->second;
        }
        return default_label();
    }

    // Hook for subclasses that want to compose a label from manifest
    // metadata + config (e.g. a SCSI HDD that folds its target ID
    // into "Hard Disc (SCSI ID 0)" so multiple drives are visually
    // distinct in the sidebar).
    //
    // The base implementation returns the manifest display_name, or
    // the instance id if no display_name is set -- preserving the
    // pre-override fallback chain for extensions that need no
    // composition.
    virtual std::string default_label() const {
        if (!manifest_.display_name.empty()) {
            return manifest_.display_name;
        }
        return std::string(id());
    }

    // Extension name (default reads from manifest; can be overridden).
    virtual std::string_view name() const { return manifest_.name; }

    // Human-readable description (from manifest).
    std::string_view description() const { return manifest_.description; }

    // Optional UI hook for the Extension UI framework. Returns nullptr
    // by default (extension exposes no UI). Concrete extensions that
    // want to surface a control panel in frontends override this and
    // return a stable pointer to their ExtensionUi implementation. The
    // returned pointer must outlive the Extension instance; typically
    // the implementation is a member of the concrete extension class.
    virtual ExtensionUi* ui() { return nullptr; }

    // Optional storage-capability hook. Returns nullptr by default;
    // extensions that publish storage devices (hard discs, RAM discs,
    // microdrives, ...) multiply-inherit ExtensionStorage and
    // override this to return `this`. Mirrors the ui() pattern: a
    // virtual accessor avoids requiring dynamic_cast (and its cross-
    // plugin typeinfo dependency) at the framework level.
    virtual ExtensionStorage* storage() { return nullptr; }

    // Optional remote-control hook. Returns the extension's RPC dispatchers
    // (zero or more), each serving one logical service over the core-hosted
    // ExtensionRpc channel. Empty by default. The dispatchers handle serialized
    // request/response bytes, so the extension never links gRPC -- this is the
    // sole way an extension exposes a client-facing API. The returned pointers
    // must outlive the Extension instance (typically members of the concrete
    // class). See docs/discussion/extension-rpc-channel.md.
    virtual std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() { return {}; }

protected:
    ExtensionManifest manifest_;
    std::map<std::string, std::string> config_;
    std::map<std::string, std::vector<std::string>> list_config_;
};

// Compose a stable, unique instance id for an extension of the given
// type. Used by both ExtensionRegistry::register_extension and
// EconetTransportRegistry::add to fill in config["id"] when the user
// didn't supply one.
//
// Algorithm: try `manifest_name` first; if that's in `existing_ids`,
// try `manifest_name-1`, `-2`, ... until a free candidate is found.
// Iteration order is deterministic and fills gaps left by previously
// removed instances, so the ids assigned for a given configuration
// are predictable run over run.
BEEBIUM_EXT_API std::string make_extension_id(
    std::string_view manifest_name,
    std::span<const std::string> existing_ids);

// Validate that `path` holds an image that read_rom_image could load into an
// expected_size buffer: the file exists and is either expected_size bytes, or
// 2*expected_size bytes with an all-0xFF lower half (a padded EPROM dump).
// Throws std::runtime_error naming `path` for a missing file, any other size,
// or a double-size file whose lower half is not blank. Used by the plugin
// loader's load-time presence check.
BEEBIUM_EXT_API void validate_rom_image(const std::filesystem::path& path,
                                        std::uint64_t expected_size);

// Read a ROM image from `path` into `dest` (exactly dest.size() bytes),
// accepting an exact-size file or a double-size file with an all-0xFF lower
// half (using the upper half) and logging which form it found. Throws as
// validate_rom_image does.
BEEBIUM_EXT_API void read_rom_image(const std::filesystem::path& path,
                                    std::span<std::uint8_t> dest);

}  // namespace beebium

#endif  // BEEBIUM_EXTENSION_EXTENSION_HPP

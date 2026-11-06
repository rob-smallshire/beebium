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

#include "keyboard/MappingFile.hpp"

#include "keyboard/EvdevCodes.hpp"

#include <algorithm>
#include <iterator>

namespace beebium::linux_client {

namespace {

constexpr std::pair<std::string_view, HostModifiers> MODIFIER_NAMES[] = {
    {"Shift", MODIFIER_SHIFT},
    {"Control", MODIFIER_CONTROL},
    {"Alt", MODIFIER_ALT},
    {"Meta", MODIFIER_META},
};

// Whether a "when" predicate's value names Linux among the platforms (3.5):
// a name, or an array of names; any other shape does not hold.
bool platform_holds(const Json& value) {
    if (value.is_string()) {
        return value.get<std::string>() == "linux";
    }
    if (!value.is_array()) {
        return false;
    }
    bool named = false;
    for (const auto& name : value) {
        if (!name.is_string()) {
            return false;
        }
        named = named || name.get<std::string>() == "linux";
    }
    return named;
}

// Whether a "when.keyboard" value names a class among those attached (3.5):
// a class, or an array of them; any other shape does not hold.
bool keyboard_holds(const Json& value, const KeyboardClasses& keyboards) {
    if (value.is_string()) {
        return keyboards.count(value.get<std::string>()) > 0;
    }
    if (!value.is_array()) {
        return false;
    }
    bool named = false;
    for (const auto& name : value) {
        if (!name.is_string()) {
            return false;
        }
        named = named || keyboards.count(name.get<std::string>()) > 0;
    }
    return named;
}

// Whether every predicate of an entry's "when" holds here (3.5). A predicate
// this client does not know does not hold.
bool when_holds(const Json& when, const KeyboardClasses& keyboards) {
    if (!when.is_object()) {
        return false;
    }
    for (const auto& [name, value] : when.items()) {
        const bool holds = name == "platform"   ? platform_holds(value)
                           : name == "keyboard" ? keyboard_holds(value, keyboards)
                                                : false;
        if (!holds) {
            return false;
        }
    }
    return true;
}

std::optional<MappingEntry> parse_entry(const Json& source, const KeyboardClasses& keyboards,
                                        std::string& error) {
    if (!source.is_object()) {
        error = "an entry is not an object";
        return std::nullopt;
    }
    MappingEntry entry;
    entry.source = source;
    if (source.contains("when") && !when_holds(source["when"], keyboards)) {
        entry.applies = false;
        return entry;
    }

    const bool has_code = source.contains("code");
    const bool has_platform = source.contains("platform");
    if (has_code == has_platform) {
        error = "an entry needs exactly one of code and platform";
        return std::nullopt;
    }
    if (has_code) {
        if (!source["code"].is_string()) {
            error = "an entry's code is not a string";
            return std::nullopt;
        }
        entry.code = source["code"].get<std::string>();
    } else {
        if (!source["platform"].is_string()) {
            error = "an entry's platform is not a string";
            return std::nullopt;
        }
        entry.platform = source["platform"].get<std::string>();
        if (*entry.platform == "linux") {
            if (!source.contains("scancode") || !source["scancode"].is_number_unsigned()) {
                error = "a linux platform entry needs a scancode";
                return std::nullopt;
            }
            entry.linux_scancode = source["scancode"].get<std::uint32_t>();
            if (!w3c_code_for_evdev(*entry.linux_scancode).empty()) {
                error = "a linux platform entry names scancode " +
                        std::to_string(*entry.linux_scancode) + ", which has the code " +
                        std::string(w3c_code_for_evdev(*entry.linux_scancode));
                return std::nullopt;
            }
        }
    }

    if (source.contains("with")) {
        if (!source["with"].is_array()) {
            error = "an entry's with is not an array";
            return std::nullopt;
        }
        for (const auto& name : source["with"]) {
            const auto* known = std::find_if(
                std::begin(MODIFIER_NAMES), std::end(MODIFIER_NAMES),
                [&](const auto& pair) { return name.is_string() && name.get<std::string>() == pair.first; });
            if (known == std::end(MODIFIER_NAMES)) {
                error = "unknown host modifier " + name.dump();
                return std::nullopt;
            }
            entry.with |= known->second;
        }
    }

    if (!source.contains("bbc") || !source["bbc"].is_object() ||
        !source["bbc"].contains("key") || !source["bbc"]["key"].is_string()) {
        error = "an entry needs a bbc object with a key";
        return std::nullopt;
    }
    const Json& bbc = source["bbc"];
    entry.bbc_key = bbc["key"].get<std::string>();
    entry.bbc_shift = bbc.value("shift", false);
    entry.bbc_ctrl = bbc.value("ctrl", false);
    if (bbc.contains("disabled")) {
        entry.disabled = bbc["disabled"].get<bool>();
    }
    if (bbc.contains("action")) {
        entry.action = bbc["action"].get<std::string>();
    }
    return entry;
}

Json write_entry(const MappingEntry& entry) {
    if (!entry.applies) {
        return entry.source;
    }
    Json out = entry.source.is_object() ? entry.source : Json::object();
    if (entry.code) {
        out["code"] = *entry.code;
    }
    if (entry.platform) {
        out["platform"] = *entry.platform;
    }
    if (entry.with != 0) {
        Json with = Json::array();
        for (const auto& [name, bit] : MODIFIER_NAMES) {
            if (entry.with & bit) {
                with.push_back(name);
            }
        }
        out["with"] = with;
    } else {
        out.erase("with");
    }
    Json bbc = out.contains("bbc") && out["bbc"].is_object() ? out["bbc"] : Json::object();
    bbc["key"] = entry.bbc_key;
    if (entry.bbc_shift) {
        bbc["shift"] = true;
    } else if (bbc.contains("shift")) {
        bbc["shift"] = false;
    }
    if (entry.bbc_ctrl) {
        bbc["ctrl"] = true;
    } else if (bbc.contains("ctrl")) {
        bbc["ctrl"] = false;
    }
    if (entry.disabled) {
        bbc["disabled"] = *entry.disabled;
    } else {
        bbc.erase("disabled");
    }
    if (entry.action) {
        bbc["action"] = *entry.action;
    } else {
        bbc.erase("action");
    }
    out["bbc"] = bbc;
    return out;
}

}  // namespace

HostModifiers modifier_for_code(std::string_view code) {
    if (code == "ShiftLeft" || code == "ShiftRight") {
        return MODIFIER_SHIFT;
    }
    if (code == "ControlLeft" || code == "ControlRight") {
        return MODIFIER_CONTROL;
    }
    if (code == "AltLeft" || code == "AltRight") {
        return MODIFIER_ALT;
    }
    if (code == "MetaLeft" || code == "MetaRight") {
        return MODIFIER_META;
    }
    return 0;
}

MappingFileResult parse_mapping_file(std::string_view json_text, const KeyboardClasses& keyboards) {
    Json source = Json::parse(json_text, nullptr, false);
    if (source.is_discarded() || !source.is_object()) {
        return MappingFileError{"not a JSON object"};
    }
    if (!source.contains("version") || !source["version"].is_number_integer()) {
        return MappingFileError{"no version"};
    }
    const int version = source["version"].get<int>();
    if (version == 1) {
        return MappingFileError{"version 1: convert it to version 2 first"};
    }
    if (version != 2) {
        return MappingFileError{"version " + std::to_string(version) +
                                " is newer than this client reads"};
    }
    for (const char* field : {"id", "name"}) {
        if (!source.contains(field) || !source[field].is_string()) {
            return MappingFileError{std::string("no ") + field};
        }
    }
    if (!source.contains("keys") || !source["keys"].is_array()) {
        return MappingFileError{"no keys"};
    }

    MappingFile file;
    file.id = source["id"].get<std::string>();
    file.name = source["name"].get<std::string>();
    file.character_mapping = source.value("characterMapping", true);
    file.synchronize_caps_lock = source.value("synchronizeCapsLock", true);
    for (const auto& item : source["keys"]) {
        std::string error;
        auto entry = parse_entry(item, keyboards, error);
        if (!entry) {
            return MappingFileError{error};
        }
        file.keys.push_back(std::move(*entry));
    }
    file.source = std::move(source);
    return file;
}

std::string write_mapping_file(const MappingFile& file) {
    Json out = file.source.is_object() ? file.source : Json::object();
    out["version"] = 2;
    out["id"] = file.id;
    out["name"] = file.name;
    out["characterMapping"] = file.character_mapping;
    out["synchronizeCapsLock"] = file.synchronize_caps_lock;
    Json keys = Json::array();
    for (const auto& entry : file.keys) {
        keys.push_back(write_entry(entry));
    }
    out["keys"] = keys;
    return out.dump(2) + "\n";
}

}  // namespace beebium::linux_client

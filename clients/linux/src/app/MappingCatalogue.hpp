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

#ifndef BEEBIUM_LINUX_APP_MAPPING_CATALOGUE_HPP
#define BEEBIUM_LINUX_APP_MAPPING_CATALOGUE_HPP

#include "keyboard/MappingFile.hpp"

#include <QString>

#include <optional>
#include <vector>

namespace beebium::linux_client {

// A mapping the user may choose (client specification 7.4): a built-in, or
// a user's version 2 file. One that failed to load is still listed, with why.
struct MappingChoice {
    QString id;
    QString name;
    bool built_in = false;
    QString filepath;  // a user's file
    std::optional<MappingFile> mapping;
    QString problem;  // why it cannot be chosen

    bool usable() const { return mapping.has_value(); }
};

// The built-in names, Logical first, as every client ships them.
const std::vector<QString>& built_in_mapping_names();

// Logical's id, the default mapping's (docs/keyboard-mapping-files.md 5).
QString logical_mapping_id();

// The mapping active before the current one, for Edit > Switch to ...
// Keyboard Mapping, as the macOS client keeps it: recorded on every change
// of active mapping, whatever made it, and held in memory only.
class MappingHistory {
public:
    explicit MappingHistory(QString active_id = {}) : active_(std::move(active_id)) {}

    // A change of active mapping: the one it replaces becomes the previous.
    void activate(const QString& id);
    // Keep the previous only while it is among the usable choices: a mapping
    // whose file has gone is forgotten, not switched to.
    void keep_only(const std::vector<MappingChoice>& choices);

    const QString& active() const { return active_; }
    const QString& previous() const { return previous_; }
    // Where the switch goes: from any mapping but Logical, to Logical; from
    // Logical, to the previous. Empty when there is nowhere to go.
    QString target() const;

private:
    QString active_;
    QString previous_;
};

// Where the user's mapping files are: $XDG_CONFIG_HOME/Beebium/keyboard-mappings.
QString user_mappings_dirpath();

// The built-ins, then the user's files in that directory by name; a file
// that does not load is listed with the reason, never skipped.
std::vector<MappingChoice> mapping_choices(const QString& user_dirpath);

// A host key as a person names it: "Alt+F12", "Z", "Left Shift", from the
// W3C code and modifiers of a mapping entry.
QString host_key_label(const MappingEntry& entry);

// The rows of a mapping's reference table (#234): every entry carrying an
// action label, built-ins included, as host key and action, in file order.
struct ReferenceRow {
    QString host_key;
    QString action;
};
std::vector<ReferenceRow> mapping_reference(const MappingFile& mapping);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MAPPING_CATALOGUE_HPP

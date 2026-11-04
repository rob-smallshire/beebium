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

#ifndef BEEBIUM_LINUX_PRESET_PARAMETER_FORM_MODEL_HPP
#define BEEBIUM_LINUX_PRESET_PARAMETER_FORM_MODEL_HPP

#include "preset/MachineDraft.hpp"
#include "preset/PresetResolution.hpp"
#include "preset/PresetSchema.hpp"

#include "extension_ui.pb.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace beebium::linux_client {

// A file or directory to choose, a local action the form leaves to its
// window: an open chooser for a file read, a save chooser for one the
// server writes, a directory chooser for a dirpath.
struct FormBrowse {
    enum class Chooser { open_file, save_file, directory };
    std::string key;
    Chooser chooser = Chooser::open_file;
    std::string current;
    std::string label;  // the parameter's, for the chooser's title
};

// What applying an action did.
struct FormOutcome {
    bool edited = false;
    std::optional<FormBrowse> browse;
};

// An extension's (or transport's) parameters as a form over the draft
// (client specification 12.2). It builds a View from the manifest, the
// draft's values at a JSON pointer prefix and the server's diagnostics,
// for the live views' renderer to show, and applies that renderer's
// actions back to the draft. So the forms commit as the live views do: on
// the end of an edit, before another control acts, and editors whole.
//
// Each kind of parameter maps to a primitive: a choice to a Choice, a
// boolean to a Toggle, an integer or string to a TextInput (an integer's
// bounds its placeholder; the server judges), a host serial port to an
// EditableChoice of the host's ports, a file or directory to a TextInput
// with a Browse button, a bundled one to an EditableChoice of the server's
// images, a list to an EditableList (a structured one item by item, by its
// item_format). Alternatives are one Choice showing the chosen member's
// field; a field another set field excludes shows read-only, saying why.
// Each parameter's diagnostics show beneath it, verbatim.
class ParameterFormModel {
public:
    ParameterFormModel(SchemaExtension extension, std::string prefix, const PresetSchema& schema);

    void set_serial_ports(std::vector<std::string> ports) { serial_ports_ = std::move(ports); }

    beebium::View view(const MachineDraft& draft,
                       const std::vector<PresetDiagnostic>& diagnostics) const;
    FormOutcome apply(const beebium::DispatchRequest& request, MachineDraft& draft) const;
    // Where a Browse came back with a path.
    void set_path(MachineDraft& draft, const std::string& key, const std::string& path) const;

private:
    const SchemaParameter* parameter(const std::string& key) const;
    std::string pointer(const std::string& key) const;
    std::optional<nlohmann::ordered_json> value(const MachineDraft& draft,
                                                const std::string& key) const;
    bool given(const MachineDraft& draft, const std::string& key) const;
    void add_parameter(beebium::Control* group, const SchemaParameter& p, const MachineDraft& draft,
                       const std::vector<PresetDiagnostic>& diagnostics) const;

    SchemaExtension extension_;
    std::string prefix_;
    std::map<std::string, std::vector<std::string>> bundled_;  // by parameter key
    std::vector<std::string> serial_ports_;
    // A group's chosen member while it has no value yet.
    mutable std::map<std::string, std::string> chosen_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_PRESET_PARAMETER_FORM_MODEL_HPP

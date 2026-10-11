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

// The extension subcommands: list-extensions, describe-extension,
// list-attachment-points and list-name-placeholders.

#pragma once

#include "beebium/server/StartOptions.hpp"
#include "beebium/extension/AttachmentPointCatalogue.hpp"
#include "beebium/extension/AttachmentPlan.hpp"
#include "beebium/service/NamePlaceholders.hpp"
#include "AunMapFile.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

namespace extension_listing {

// Minimal JSON string escaping for description / name fields.
inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Render a string list as a JSON array, e.g. ["serial-port","user-port"].
inline std::string json_string_array(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += "\"" + json_escape(items[i]) + "\"";
    }
    out += "]";
    return out;
}

// Join a string list with commas for a single TSV / pretty cell.
inline std::string join_comma(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += items[i];
    }
    return out;
}

// Parse just `--extension-dir <path>` arguments out of a subcommand argv slice.
// Other tokens are reported as "unknown argument" by the caller.
struct ExtensionDirParse {
    std::vector<std::string> extension_dirpaths;
    std::vector<std::string> remaining;  // tokens that were not --extension-dir
};

inline ExtensionDirParse parse_extension_dir_args(int argc, char* argv[], int start) {
    ExtensionDirParse result;
    for (int i = start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--extension-dir" && i + 1 < argc) {
            result.extension_dirpaths.emplace_back(argv[++i]);
            continue;
        }
        result.remaining.emplace_back(arg);
    }
    return result;
}

}  // namespace extension_listing

// ListExtensions subcommand - list all available extensions
template<typename MachineType>
class ListExtensionsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-extensions"; }
    std::string_view description() const override {
        return "List available peripheral and transport extensions";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " list-extensions [--attaches-to <point>] [--extension-dir <path>]...\n"
                  << "\n"
                  << "Lists every extension that can be selected via a --<cli-name> flag,\n"
                  << "in the same priority order as `start` would resolve them: built-in\n"
                  << "extensions, then the auto-detected <exe-dir>/extensions directory,\n"
                  << "then each --extension-dir given here. Later sources override earlier\n"
                  << "ones for matching cli-name.\n"
                  << "\n"
                  << "  --attaches-to <point>  show only extensions that attach to the given\n"
                  << "                         attachment point (e.g. serial-port); see\n"
                  << "                         list-attachment-points for the points.\n"
                  << "\n"
                  << "Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        auto parsed = extension_listing::parse_extension_dir_args(
            argc, argv, global.subcommand_argv_start);
        std::string attaches_to_filter;
        for (std::size_t i = 0; i < parsed.remaining.size(); ++i) {
            const std::string& tok = parsed.remaining[i];
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            if (tok == "--attaches-to" && i + 1 < parsed.remaining.size()) {
                attaches_to_filter = parsed.remaining[++i];
                continue;
            }
            std::cerr << "Unknown argument: " << tok << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        if (auto err = populate_extension_resolver(argv[0], parsed.extension_dirpaths, config)) {
            return *err;
        }

        OutputFormat format = resolve_output_format(global.output_format);

        // Optionally filter to extensions that attach to a given point. A point
        // appears in an extension's attaches_to list (which may name several).
        const auto& all_entries = config.launch.extension_resolver.entries();
        std::vector<const typename std::decay_t<decltype(all_entries)>::value_type*> entries;
        for (const auto& e : all_entries) {
            if (!attaches_to_filter.empty()) {
                const auto& a = e.manifest->attaches_to;
                if (std::find(a.begin(), a.end(), attaches_to_filter) == a.end()) {
                    continue;
                }
            }
            entries.push_back(&e);
        }

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Available extensions:\n";
                for (const auto* e : entries) {
                    std::cout << "  --" << e->manifest->effective_cli_name();
                    if (e->manifest->name != e->manifest->effective_cli_name()) {
                        std::cout << " (" << e->manifest->name << ")";
                    }
                    std::cout << " [" << e->source_label << "]\n";
                    if (!e->manifest->attaches_to.empty()) {
                        std::cout << "      attaches to: "
                                  << extension_listing::join_comma(e->manifest->attaches_to)
                                  << "\n";
                    }
                    if (!e->manifest->description.empty()) {
                        std::cout << "      " << e->manifest->description << "\n";
                    }
                }
                if (entries.empty()) {
                    std::cout << "  (no extensions found)\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "cli_name\tname\tkind\tsource\tattaches_to\tdescription\n";
                for (const auto* e : entries) {
                    std::cout << e->manifest->effective_cli_name() << "\t"
                              << e->manifest->name << "\t"
                              << e->manifest->extension_kind << "\t"
                              << e->source_label << "\t"
                              << extension_listing::join_comma(e->manifest->attaches_to) << "\t"
                              << e->manifest->description << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto* e : entries) {
                    std::cout << "{\"cli_name\":\""
                              << extension_listing::json_escape(e->manifest->effective_cli_name())
                              << "\",\"name\":\""
                              << extension_listing::json_escape(e->manifest->name)
                              << "\",\"kind\":\""
                              << extension_listing::json_escape(e->manifest->extension_kind)
                              << "\",\"source\":\""
                              << extension_listing::json_escape(e->source_label)
                              << "\",\"attaches_to\":"
                              << extension_listing::json_string_array(e->manifest->attaches_to)
                              << ",\"description\":\""
                              << extension_listing::json_escape(e->manifest->description)
                              << "\"}\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// DescribeExtension subcommand - parameter detail for a single extension
// One manifest parameter as describe-extension's JSON forms give it: every
// property a configuration UI needs to build a field for it.
inline nlohmann::ordered_json choices_json(const std::vector<beebium::ParameterChoice>& choices) {
    nlohmann::ordered_json out = nlohmann::ordered_json::array();
    for (const auto& c : choices) {
        out.push_back({{"value", c.value}, {"label", c.label}, {"description", c.description}});
    }
    return out;
}

inline nlohmann::ordered_json bound_json(const std::optional<long long>& bound) {
    return bound ? nlohmann::ordered_json(*bound) : nlohmann::ordered_json(nullptr);
}

inline nlohmann::ordered_json parameter_schema_json(const beebium::ParameterSchema& p) {
    nlohmann::ordered_json fields = nlohmann::ordered_json::array();
    for (const auto& f : p.fields) {
        fields.push_back({
            {"key", f.key},
            {"label", f.label},
            {"description", f.description},
            {"type", f.type},
            {"required", f.required},
            {"default", f.default_value},
            {"min", bound_json(f.min)},
            {"max", bound_json(f.max)},
            {"choices", choices_json(f.choices)},
        });
    }
    return {
        {"key", p.key},
        {"label", p.label},
        {"description", p.description},
        {"type", p.type},
        {"required", p.required},
        {"is_list", p.is_list},
        {"position", p.position >= 0 ? nlohmann::ordered_json(p.position)
                                     : nlohmann::ordered_json(nullptr)},
        {"default", p.default_value},
        {"default_from", p.default_from},
        {"effective_default", effective_default(p)},
        {"min", bound_json(p.min)},
        {"max", bound_json(p.max)},
        {"choices", choices_json(p.choices)},
        {"choices_from", p.choices_from},
        {"excludes", p.excludes},
        {"bundled", p.bundled},
        {"unique", p.unique},
        {"provider_of", p.provider_of},
        {"item_format", p.item_format},
        {"fields", fields},
    };
}

// An extension as describe-extension --json gives it.
inline nlohmann::ordered_json extension_manifest_json(const beebium::ExtensionManifest& m,
                                                      const std::string& source) {
    nlohmann::ordered_json parameters = nlohmann::ordered_json::array();
    for (const auto& p : m.parameters) parameters.push_back(parameter_schema_json(p));
    nlohmann::ordered_json roms = nlohmann::ordered_json::array();
    for (const auto& r : m.roms) {
        roms.push_back({{"key", r.key}, {"filename", r.filename}, {"size", r.size},
                        {"display_name", r.display_name}});
    }
    nlohmann::ordered_json groups = nlohmann::ordered_json::array();
    for (const auto& g : m.groups) {
        nlohmann::ordered_json group = {{"key", g.key},
                                        {"label", g.label},
                                        {"description", g.description},
                                        {"parameters", g.parameters}};
        if (g.none) {
            group["none"] = {{"label", g.none->label}, {"description", g.none->description}};
        }
        groups.push_back(std::move(group));
    }
    return {
        {"name", m.name},
        {"display_name", m.display_name},
        {"description", m.description},
        {"cli_name", std::string(m.effective_cli_name())},
        {"kind", m.extension_kind},
        {"source", source},
        {"attaches_to", m.attaches_to},
        {"provides", m.provides},
        {"parameters", parameters},
        {"groups", groups},
        {"roms", roms},
    };
}

template<typename MachineType>
class DescribeExtensionSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-extension"; }
    std::string_view description() const override {
        return "Show parameter detail for a specific extension";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " describe-extension <name> [--extension-dir <path>]...\n"
                  << "\n"
                  << "Shows the parameter schema for an extension. <name> may be\n"
                  << "either the CLI flag stem (e.g. tube-65c02) or the canonical\n"
                  << "extension name (e.g. acorn-65c02-coprocessor).\n"
                  << "Honours --format pretty|tsv|jsonl (jsonl: one object per\n"
                  << "parameter). --json prints the whole extension as one object:\n"
                  << "name, display_name, description, cli_name, kind, source,\n"
                  << "attaches_to, provides, parameters (key, label, description,\n"
                  << "type, required, is_list, position, default, default_from,\n"
                  << "effective_default, min, max, choices, choices_from, excludes,\n"
                  << "bundled, unique, provider_of, item_format, fields), groups (key,\n"
                  << "label, description,\n"
                  << "parameters, and none when the group names the state with no member\n"
                  << "given) and roms.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        auto parsed = extension_listing::parse_extension_dir_args(
            argc, argv, global.subcommand_argv_start);

        std::string target_name;
        bool json_output = false;
        for (const auto& tok : parsed.remaining) {
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            if (tok == "--json") {
                json_output = true;
                continue;
            }
            if (!tok.empty() && tok.front() == '-') {
                std::cerr << "Unknown argument: " << tok << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
            if (!target_name.empty()) {
                std::cerr << "Error: describe-extension takes a single name argument\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
            target_name = tok;
        }

        if (target_name.empty()) {
            std::cerr << "Error: describe-extension requires an extension name\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        if (auto err = populate_extension_resolver(argv[0], parsed.extension_dirpaths, config)) {
            return *err;
        }

        // Look up by cli_name first, then by canonical name.
        const beebium::ExtensionManifest* manifest = nullptr;
        std::string source_label;
        for (const auto& e : config.launch.extension_resolver.entries()) {
            if (e.manifest->effective_cli_name() == target_name
                || e.manifest->name == target_name) {
                manifest = e.manifest;
                source_label = e.source_label;
                break;
            }
        }

        if (!manifest) {
            std::cerr << "Error: no extension named '" << target_name << "'\n";
            return ExitCode::CONFIG;
        }

        if (json_output) {
            std::cout << extension_manifest_json(*manifest, source_label).dump(2) << "\n";
            return ExitCode::OK;
        }

        OutputFormat format = resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty: {
                std::cout << "--" << manifest->effective_cli_name();
                if (manifest->name != manifest->effective_cli_name()) {
                    std::cout << " (" << manifest->name << ")";
                }
                std::cout << " [" << source_label << "]\n";
                if (!manifest->description.empty()) {
                    std::cout << "  " << manifest->description << "\n";
                }
                // Synthesised invocation form: the colon-separated key=value
                // syntax the generic extension arg parser accepts. Required
                // params are shown bare; optional ones are bracketed.
                std::cout << "  Usage: --" << manifest->effective_cli_name();
                if (!manifest->parameters.empty()) {
                    std::cout << " ";
                    bool first = true;
                    for (const auto& p : manifest->parameters) {
                        std::string token = p.key + "=<" + p.type + ">";
                        if (p.required) {
                            std::cout << (first ? "" : ":") << token;
                        } else {
                            std::cout << "[" << (first ? "" : ":") << token << "]";
                        }
                        first = false;
                    }
                    std::cout << "\n         (colon-separated key=value pairs)\n";
                } else {
                    std::cout << "\n";
                }
                std::cout << "  kind: " << manifest->extension_kind << "\n";
                if (!manifest->attaches_to.empty()) {
                    std::cout << "  attaches to:";
                    for (const auto& a : manifest->attaches_to) {
                        std::cout << " " << a << " (" << attachment_point_display_name(a) << ")";
                    }
                    std::cout << "\n";
                }
                if (!manifest->provides.empty()) {
                    std::cout << "  provides:";
                    for (const auto& p : manifest->provides) std::cout << " " << p;
                    std::cout << "\n";
                }
                if (manifest->parameters.empty()) {
                    std::cout << "  (no parameters)\n";
                } else {
                    std::cout << "  Parameters:\n";
                    for (const auto& p : manifest->parameters) {
                        std::cout << "    " << p.key
                                  << " : " << p.type;
                        if (p.is_list) std::cout << " (list)";
                        if (p.required) std::cout << " (required)";
                        if (p.min || p.max) {
                            std::cout << " (" << (p.min ? std::to_string(*p.min) : "")
                                      << ".." << (p.max ? std::to_string(*p.max) : "") << ")";
                        }
                        if (!p.excludes.empty()) {
                            std::cout << " (not with";
                            for (const auto& k : p.excludes) std::cout << " " << k;
                            std::cout << ")";
                        }
                        if (!p.choices.empty()) {
                            std::cout << " {";
                            for (std::size_t c = 0; c < p.choices.size(); ++c) {
                                std::cout << (c ? "|" : "") << p.choices[c].value;
                            }
                            std::cout << "}";
                        }
                        if (auto value = effective_default(p); !value.empty()) {
                            std::cout << " [default: " << value << "]";
                        }
                        if (!p.bundled.empty()) {
                            std::cout << " (or a bundled " << p.bundled << " image)";
                        }
                        if (p.unique) std::cout << " (unique on its attachment)";
                        if (!p.provider_of.empty()) {
                            std::cout << " (names the " << p.provider_of << " provider)";
                        }
                        std::cout << "\n";
                        if (!p.label.empty() || !p.description.empty()) {
                            std::cout << "        " << p.label
                                      << (p.label.empty() || p.description.empty() ? "" : ": ")
                                      << p.description << "\n";
                        }
                        if (!p.item_format.empty()) {
                            std::cout << "        each item: " << p.item_format << "\n";
                        }
                        for (const auto& f : p.fields) {
                            std::cout << "          " << f.key << " : " << f.type;
                            if (f.required) std::cout << " (required)";
                            if (f.min || f.max) {
                                std::cout << " (" << (f.min ? std::to_string(*f.min) : "")
                                          << ".." << (f.max ? std::to_string(*f.max) : "")
                                          << ")";
                            }
                            if (!f.default_value.empty()) {
                                std::cout << " [default: " << f.default_value << "]";
                            }
                            if (!f.label.empty()) std::cout << " -- " << f.label;
                            std::cout << "\n";
                        }
                    }
                }
                for (const auto& g : manifest->groups) {
                    std::cout << "  " << g.label << " (" << g.key << "), one of:";
                    if (g.none) std::cout << " none (" << g.none->label << ")";
                    for (const auto& k : g.parameters) std::cout << " " << k;
                    std::cout << "\n";
                }
                if (!manifest->roms.empty()) {
                    std::cout << "  ROMs:\n";
                    for (const auto& r : manifest->roms) {
                        std::cout << "    " << r.key << " : " << r.filename
                                  << " (" << r.size << " bytes)";
                        if (!r.display_name.empty()) {
                            std::cout << " -- " << r.display_name;
                        }
                        std::cout << "\n";
                    }
                }
                break;
            }
            case OutputFormat::Tsv:
                std::cout << "key\ttype\tis_list\trequired\tdefault\tdescription\n";
                for (const auto& p : manifest->parameters) {
                    std::cout << p.key << "\t" << p.type << "\t"
                              << (p.is_list ? "true" : "false") << "\t"
                              << (p.required ? "true" : "false") << "\t"
                              << p.default_value << "\t"
                              << p.description << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& p : manifest->parameters) {
                    std::cout << parameter_schema_json(p).dump() << "\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// ListAttachmentPoints subcommand - the catalogue of points an extension can
// attach to, with human-readable names. Lets a configuration UI ask, for each
// point, "which of these <point> extensions would you like to load?".
template<typename MachineType>
class ListAttachmentPointsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-attachment-points"; }
    std::string_view description() const override {
        return "List the attachment points extensions can plug into";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-attachment-points\n"
                  << "\n"
                  << "Lists the attachment points an extension can declare via attaches_to\n"
                  << "(serial-port, user-port, 1mhz-bus, tube, scsi), each with a display\n"
                  << "name and its occupancy: 'single' (a connector that holds at most one\n"
                  << "extension) or 'multi' (a bus that several may share). Pair with\n"
                  << "`list-extensions --attaches-to <point>` to see what plugs in where.\n"
                  << "Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view tok = argv[i];
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << tok << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        OutputFormat format = resolve_output_format(global.output_format);
        const auto& points = attachment_point_catalogue();

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Attachment points:\n";
                for (const auto& p : points) {
                    std::cout << "  " << p.id << " (" << p.display_name << ") ["
                              << occupancy_label(p) << "]\n";
                    if (!p.description.empty()) {
                        std::cout << "      " << p.description << "\n";
                    }
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "id\tdisplay_name\tmin_occupancy\tmax_occupancy\tdescription\n";
                for (const auto& p : points) {
                    std::cout << p.id << "\t" << p.display_name << "\t"
                              << p.min_occupancy << "\t"
                              << (p.max_occupancy ? std::to_string(*p.max_occupancy) : "")
                              << "\t" << p.description << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& p : points) {
                    std::cout << "{\"id\":\"" << extension_listing::json_escape(p.id)
                              << "\",\"display_name\":\""
                              << extension_listing::json_escape(p.display_name)
                              << "\",\"min_occupancy\":" << p.min_occupancy
                              << ",\"max_occupancy\":"
                              << (p.max_occupancy ? std::to_string(*p.max_occupancy) : "null")
                              << ",\"description\":\""
                              << extension_listing::json_escape(p.description)
                              << "\"}\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// ListNamePlaceholders subcommand: the placeholders a machine-name template
// can use (#153), for people writing presets and launch commands. Lists the
// core's placeholders without launching a machine, so only values fixed
// before launch are shown; a running server's ListNamePlaceholders RPC lists
// every placeholder, extensions' included, with live values.
template<typename MachineType>
class ListNamePlaceholdersSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-name-placeholders"; }
    std::string_view description() const override {
        return "List the placeholders a machine-name template can use";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-name-placeholders\n"
                  << "\n"
                  << "Lists the placeholders a machine-name template (--machine-name, a\n"
                  << "preset's machine_name) can use: each placeholder's key, written\n"
                  << "{key} in a template, its label, group and description, and its\n"
                  << "value where it is known before launch. 'Station {econet-station}'\n"
                  << "renders as 'Station 80' on station 80, and follows a renumber at\n"
                  << "the next Break. {{ and }} are literal braces; an unknown key\n"
                  << "renders as written. Extensions can add placeholders; a running\n"
                  << "server lists them all. Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view tok = argv[i];
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << tok << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        using Memory = typename MachineType::Memory;
        std::vector<beebium::NamePlaceholderInfo> infos =
            beebium::service::machine_name_placeholder_infos();
        for (auto& info : beebium::service::econet_name_placeholder_infos()) {
            infos.push_back(std::move(info));
        }
        for (auto& info : beebium::service::storage_name_placeholder_infos()) {
            infos.push_back(std::move(info));
        }
        for (auto& info : beebium::service::coprocessor_name_placeholder_infos()) {
            infos.push_back(std::move(info));
        }
        // The one value fixed before launch.
        auto known_value = [](const std::string& key) -> std::optional<std::string> {
            if (key == "machine-model") return std::string(Memory::MACHINE_DISPLAY_NAME);
            return std::nullopt;
        };

        OutputFormat format = resolve_output_format(global.output_format);
        switch (format) {
            case OutputFormat::Pretty: {
                std::string group;
                for (const auto& info : infos) {
                    if (info.group != group) {
                        group = info.group;
                        std::cout << group << ":\n";
                    }
                    std::cout << "  {" << info.key << "}  " << info.label;
                    if (auto value = known_value(info.key)) {
                        std::cout << " = " << *value;
                    }
                    std::cout << "\n      " << info.description << "\n";
                }
                break;
            }
            case OutputFormat::Tsv:
                std::cout << "key\tlabel\tgroup\tvalue\tdescription\n";
                for (const auto& info : infos) {
                    std::cout << info.key << "\t" << info.label << "\t" << info.group << "\t"
                              << known_value(info.key).value_or("") << "\t"
                              << info.description << "\n";
                }
                break;
            case OutputFormat::Jsonl:
                for (const auto& info : infos) {
                    auto value = known_value(info.key);
                    std::cout << "{\"key\":\"" << extension_listing::json_escape(info.key)
                              << "\",\"label\":\"" << extension_listing::json_escape(info.label)
                              << "\",\"group\":\"" << extension_listing::json_escape(info.group)
                              << "\",\"insertion\":\"{"
                              << extension_listing::json_escape(info.key)
                              << "}\",\"value\":"
                              << (value ? "\"" + extension_listing::json_escape(*value) + "\""
                                        : std::string("null"))
                              << ",\"description\":\""
                              << extension_listing::json_escape(info.description) << "\"}\n";
                }
                break;
            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

} // namespace beebium::server

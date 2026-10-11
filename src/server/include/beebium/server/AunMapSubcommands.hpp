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

// The AUN map subcommands: report-aun-map-filepath, create-aun-map,
// show-aun-map, add-aun-peer, remove-aun-peer, add-aun-subnet and
// remove-aun-subnet.

#pragma once

#include "beebium/server/SubcommandCommon.hpp"
#include "AunMapFile.hpp"
#include "AunMapWriter.hpp"
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

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

// ---- AUN map file subcommands ----
//
// Edit the per-user aun-map.json through the same library the AUN transport
// uses at runtime. Model-independent: pure file work, no machine is built.

struct AunMapArgs {
    std::vector<std::string> positional;
    std::filesystem::path filepath;
    std::string label;
    bool help = false;
    std::string unknown_flag;  // first unrecognised --flag, if any
};

inline AunMapArgs parse_aun_map_args(int argc, char* argv[],
                                     const GlobalConfig& global) {
    AunMapArgs out;
    bool have_mapfile = false;
    for (int i = global.subcommand_argv_start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            out.help = true;
        } else if (arg == "--map-file" && i + 1 < argc) {
            out.filepath = argv[++i];
            have_mapfile = true;
        } else if (arg.rfind("--map-file=", 0) == 0) {
            out.filepath = std::string(arg.substr(11));
            have_mapfile = true;
        } else if (arg == "--label" && i + 1 < argc) {
            out.label = argv[++i];
        } else if (arg.rfind("--label=", 0) == 0) {
            out.label = std::string(arg.substr(8));
        } else if (!arg.empty() && arg.front() == '-') {
            if (out.unknown_flag.empty()) out.unknown_flag = std::string(arg);
        } else {
            out.positional.push_back(std::string(arg));
        }
    }
    if (!have_mapfile) out.filepath = beebium::default_aun_map_filepath();
    return out;
}

// Parse "net.stn" (e.g. "0.254"): net 0..255, stn 1..254.
inline bool parse_aun_net_stn(const std::string& text, int& net, int& stn) {
    auto dot = text.find('.');
    if (dot == std::string::npos) return false;
    try {
        std::size_t net_len = 0, stn_len = 0;
        net = std::stoi(text.substr(0, dot), &net_len);
        stn = std::stoi(text.substr(dot + 1), &stn_len);
        if (net_len != dot || stn_len != text.size() - dot - 1) return false;
    } catch (...) {
        return false;
    }
    return net >= 0 && net <= 255 && stn >= 1 && stn <= 254;
}

// Load the document for an edit, printing a position-bearing error and setting
// `exit_code` if the file is present but malformed.
inline std::optional<beebium::AunMapDocument> open_aun_map_for_edit(
        const std::filesystem::path& filepath, int& exit_code) {
    auto load = beebium::AunMapDocument::load(filepath);
    if (!load.document.has_value()) {
        std::cerr << load.error << "\n";
        exit_code = ExitCode::DATAERR;
        return std::nullopt;
    }
    return std::move(load.document);
}

template<typename MachineType>
class ReportAunMapFilepathSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "report-aun-map-filepath"; }
    std::string_view description() const override {
        return "Report the AUN map file path";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " report-aun-map-filepath [--map-file <path>]\n\n"
                  << "Outputs the per-user aun-map.json path, after the "
                     "--map-file option and the\nBEEBIUM_AUN_MAP_FILEPATH "
                     "environment variable override.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        std::cout << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class CreateAunMapSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-aun-map"; }
    std::string_view description() const override {
        return "Create a template AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " create-aun-map [--map-file <path>]\n\n"
                  << "Writes a template aun-map.json with one example peer and "
                     "one example subnet,\nwhose labels explain the "
                     "PiEconetBridge and RISC OS cases. Refuses to overwrite an\n"
                     "existing file.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        std::error_code ec;
        if (std::filesystem::exists(args.filepath, ec)) {
            std::cerr << "Refusing to overwrite existing file: "
                      << args.filepath.string() << "\n";
            return ExitCode::CONFIG;
        }
        auto doc = beebium::AunMapDocument::empty();
        doc.add_or_replace_peer(
            0, 254, "192.168.1.10", 32768,
            "PiEconetBridge: file server 1.254 exposed as 0.254");
        doc.add_or_replace_subnet(
            128, "192.168.1.0/24",
            "RISC OS convention: net 128 is this /24, station = last octet, "
            "port 32768");
        if (std::string err = doc.save(args.filepath); !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::IOERR;
        }
        std::cout << "Created " << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class ShowAunMapSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "show-aun-map"; }
    std::string_view description() const override {
        return "Show the AUN map file's entries";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " [--format pretty|tsv|jsonl] show-aun-map [--map-file <path>]\n\n"
                  << "Shows the parsed peers and subnets with labels and host "
                     "resolution.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        auto loaded = beebium::load_aun_map(args.filepath);
        if (!loaded.map.has_value()) {
            std::cerr << loaded.error << "\n";
            return ExitCode::DATAERR;
        }
        OutputFormat format = resolve_output_format(global.output_format);
        const auto& map = *loaded.map;
        if (format == OutputFormat::Jsonl) {
            for (const auto& p : map.peers) {
                auto ip = beebium::resolve_map_host(p.host);
                nlohmann::json row{{"kind", "peer"}, {"net", p.net},
                                   {"station", p.stn}, {"host", p.host},
                                   {"port", p.port}, {"label", p.label},
                                   {"resolved", ip.has_value()},
                                   {"resolved_ip", ip.value_or("")}};
                std::cout << row.dump() << "\n";
            }
            for (const auto& s : map.subnets) {
                nlohmann::json row{{"kind", "subnet"}, {"net", s.net},
                                   {"subnet", s.subnet_text}, {"label", s.label}};
                std::cout << row.dump() << "\n";
            }
            return ExitCode::OK;
        }
        if (format == OutputFormat::Tsv) {
            std::cout << "kind\tnet\tstn\thost_or_subnet\tport\tlabel\tresolved\n";
            for (const auto& p : map.peers) {
                auto ip = beebium::resolve_map_host(p.host);
                std::cout << "peer\t" << int(p.net) << "\t" << int(p.stn) << "\t"
                          << p.host << "\t" << p.port << "\t" << p.label << "\t"
                          << (ip ? *ip : std::string("unresolved")) << "\n";
            }
            for (const auto& s : map.subnets) {
                std::cout << "subnet\t" << int(s.net) << "\t\t" << s.subnet_text
                          << "\t\t" << s.label << "\t\n";
            }
            return ExitCode::OK;
        }
        // Pretty.
        std::cout << "Map file: " << args.filepath.string() << "\n";
        std::cout << "Peers:\n";
        if (map.peers.empty()) std::cout << "  (none)\n";
        for (const auto& p : map.peers) {
            auto ip = beebium::resolve_map_host(p.host);
            std::cout << "  " << int(p.net) << "." << int(p.stn) << "  " << p.host
                      << ":" << p.port
                      << (ip ? ("  [" + *ip + "]") : std::string("  [unresolved]"));
            if (!p.label.empty()) std::cout << "  " << p.label;
            std::cout << "\n";
        }
        std::cout << "Subnets:\n";
        if (map.subnets.empty()) std::cout << "  (none)\n";
        for (const auto& s : map.subnets) {
            std::cout << "  net " << int(s.net) << " = " << s.subnet_text;
            if (!s.label.empty()) std::cout << "  " << s.label;
            std::cout << "\n";
        }
        auto doc = beebium::AunMapDocument::load(args.filepath);
        if (doc.document.has_value()) {
            auto unknown = doc.document->unknown_top_level_keys();
            if (!unknown.empty()) {
                std::cout << "Unknown keys preserved:";
                for (const auto& key : unknown) std::cout << " " << key;
                std::cout << "\n";
            }
        }
        return ExitCode::OK;
    }
};

template<typename MachineType>
class AddAunPeerSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "add-aun-peer"; }
    std::string_view description() const override {
        return "Add or replace a peer in the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " add-aun-peer <net.stn> <host> <port> [--label <text>] "
                     "[--map-file <path>]\n\n"
                  << "Adds or replaces the peers[] entry for net.stn.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 3) {
            std::cerr << "Expected <net.stn> <host> <port>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0, stn = 0;
        if (!parse_aun_net_stn(args.positional[0], net, stn)) {
            std::cerr << "Invalid net.stn '" << args.positional[0]
                      << "' (net 0-255, station 1-254)\n";
            return ExitCode::USAGE;
        }
        int port = 0;
        try {
            std::size_t len = 0;
            port = std::stoi(args.positional[2], &len);
            if (len != args.positional[2].size() || port < 1 || port > 65535) {
                throw std::invalid_argument("");
            }
        } catch (...) {
            std::cerr << "Invalid port '" << args.positional[2]
                      << "' (1-65535)\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        if (std::string err = doc->add_or_replace_peer(
                static_cast<std::uint8_t>(net), static_cast<std::uint8_t>(stn),
                args.positional[1], static_cast<std::uint16_t>(port), args.label);
            !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::USAGE;
        }
        if (std::string err = doc->save(args.filepath); !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::IOERR;
        }
        std::cout << "Wrote " << net << "." << stn << " to "
                  << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class RemoveAunPeerSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "remove-aun-peer"; }
    std::string_view description() const override {
        return "Remove a peer from the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " remove-aun-peer <net.stn> [--map-file <path>]\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 1) {
            std::cerr << "Expected <net.stn>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0, stn = 0;
        if (!parse_aun_net_stn(args.positional[0], net, stn)) {
            std::cerr << "Invalid net.stn '" << args.positional[0] << "'\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        bool removed = doc->remove_peer(static_cast<std::uint8_t>(net),
                                        static_cast<std::uint8_t>(stn));
        if (removed) {
            if (std::string err = doc->save(args.filepath); !err.empty()) {
                std::cerr << err << "\n";
                return ExitCode::IOERR;
            }
            std::cout << "Removed " << net << "." << stn << "\n";
        } else {
            std::cout << "No entry for " << net << "." << stn << "\n";
        }
        return ExitCode::OK;
    }
};

template<typename MachineType>
class AddAunSubnetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "add-aun-subnet"; }
    std::string_view description() const override {
        return "Add or replace a subnet rule in the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " add-aun-subnet <net> <a.b.c.0/24> [--label <text>] "
                     "[--map-file <path>]\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 2) {
            std::cerr << "Expected <net> <a.b.c.0/24>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0;
        try {
            std::size_t len = 0;
            net = std::stoi(args.positional[0], &len);
            if (len != args.positional[0].size() || net < 0 || net > 255) {
                throw std::invalid_argument("");
            }
        } catch (...) {
            std::cerr << "Invalid net '" << args.positional[0] << "' (0-255)\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        if (std::string err = doc->add_or_replace_subnet(
                static_cast<std::uint8_t>(net), args.positional[1], args.label);
            !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::USAGE;
        }
        if (std::string err = doc->save(args.filepath); !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::IOERR;
        }
        std::cout << "Wrote subnet net " << net << " to "
                  << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class RemoveAunSubnetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "remove-aun-subnet"; }
    std::string_view description() const override {
        return "Remove a subnet rule from the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " remove-aun-subnet <net> [--map-file <path>]\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 1) {
            std::cerr << "Expected <net>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0;
        try {
            std::size_t len = 0;
            net = std::stoi(args.positional[0], &len);
            if (len != args.positional[0].size() || net < 0 || net > 255) {
                throw std::invalid_argument("");
            }
        } catch (...) {
            std::cerr << "Invalid net '" << args.positional[0] << "'\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        bool removed = doc->remove_subnet(static_cast<std::uint8_t>(net));
        if (removed) {
            if (std::string err = doc->save(args.filepath); !err.empty()) {
                std::cerr << err << "\n";
                return ExitCode::IOERR;
            }
            std::cout << "Removed subnet net " << net << "\n";
        } else {
            std::cout << "No subnet for net " << net << "\n";
        }
        return ExitCode::OK;
    }
};

} // namespace beebium::server

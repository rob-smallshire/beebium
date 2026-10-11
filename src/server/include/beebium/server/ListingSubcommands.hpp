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

// The subcommands that describe what the host and server offer, building no
// machine: list-fdcs, list-disc-formats, list-serial-ports, describe-rom and
// describe-disc-image.

#pragma once

#include "beebium/server/StartOptions.hpp"
#include "beebium/server/RomPaths.hpp"
#include "beebium/serial/EnumeratePorts.hpp"
#include "beebium/SidewaysRomHeader.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/disc/DiscLoader.hpp"
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

// ListFdcs subcommand - list available disc controllers
template<typename MachineType>
class ListFdcsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-fdcs"; }
    std::string_view description() const override { return "List available disc controllers"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-fdcs\n"
                  << "\n"
                  << "Lists all disc controllers that can be installed in machines\n"
                  << "with a disc controller socket (e.g., Model B).\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Resolve format based on TTY detection
        OutputFormat format = resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Available disc controllers:\n";
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << "  " << info.id << " - " << info.display_name
                              << " (" << info.fdc_chip << ")\n"
                              << "      " << info.description << "\n";
                }
                std::cout << "  none - No disc controller (leave socket empty)\n";
                break;

            case OutputFormat::Tsv:
                std::cout << "id\tdisplay_name\tfdc_chip\tdescription\n";
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << info.id << "\t" << info.display_name << "\t"
                              << info.fdc_chip << "\t" << info.description << "\n";
                }
                std::cout << "none\tNo controller\t-\tLeave socket empty (no disc)\n";
                break;

            case OutputFormat::Jsonl:
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << "{\"id\":\"" << info.id
                              << "\",\"display_name\":\"" << info.display_name
                              << "\",\"fdc_chip\":\"" << info.fdc_chip
                              << "\",\"description\":\"" << info.description << "\"}\n";
                }
                std::cout << "{\"id\":\"none\",\"display_name\":\"No controller\","
                          << "\"fdc_chip\":\"-\",\"description\":\"Leave socket empty (no disc)\"}\n";
                break;

            case OutputFormat::Auto:
                // Should not reach here after resolve_output_format
                break;
        }
        return ExitCode::OK;
    }
};

// ListDiscFormats subcommand - list the disc image formats, floppy and hard disc
template<typename MachineType>
class ListDiscFormatsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-disc-formats"; }
    std::string_view description() const override {
        return "List supported disc image formats, floppy and hard disc";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-disc-formats\n"
                  << "\n"
                  << "Lists every disc image format that can be loaded: the floppy formats,\n"
                  << "then the hard-disc formats. Each has a name, a kind (floppy or\n"
                  << "hard-disc), a description, the file extensions it is named by, and\n"
                  << "whether it can be written.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Every format, floppy then hard disc, as one list.
        struct Format {
            std::string name;
            std::string kind;
            std::string description;
            std::vector<std::string> extensions;
            bool write_support;
        };
        std::vector<Format> formats;
        for (const auto& handler : default_format_registry().handlers()) {
            Format f{std::string(handler->format_name()), "floppy",
                     std::string(handler->format_description()), {},
                     handler->supports_write()};
            for (auto ext : handler->file_extensions()) f.extensions.emplace_back(ext);
            formats.push_back(std::move(f));
        }
        for (const auto& hd : kHardDiscImageFormats) {
            formats.push_back({std::string(hd.name), "hard-disc", std::string(hd.description),
                               {std::string(hd.extension)}, hd.write_support});
        }

        auto joined = [](const std::vector<std::string>& items, std::string_view separator) {
            std::string out;
            for (const auto& item : items) {
                if (!out.empty()) out += separator;
                out += item;
            }
            return out;
        };

        switch (resolve_output_format(global.output_format)) {
            case OutputFormat::Pretty: {
                std::string kind;
                for (const auto& f : formats) {
                    if (f.kind != kind) {
                        kind = f.kind;
                        std::cout << (kind == "floppy" ? "Floppy disc image formats:\n"
                                                       : "Hard disc image formats:\n");
                    }
                    std::cout << "  " << f.name << "\n"
                              << "      " << f.description << "\n"
                              << "      Extensions: " << joined(f.extensions, " ") << "\n"
                              << "      Write support: " << (f.write_support ? "yes" : "no")
                              << "\n";
                }
                break;
            }

            case OutputFormat::Tsv:
                std::cout << "name\tkind\tdescription\textensions\twrite_support\n";
                for (const auto& f : formats) {
                    std::cout << f.name << "\t" << f.kind << "\t" << f.description << "\t"
                              << joined(f.extensions, ",") << "\t"
                              << (f.write_support ? "yes" : "no") << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& f : formats) {
                    std::cout << nlohmann::ordered_json{{"name", f.name},
                                                        {"kind", f.kind},
                                                        {"description", f.description},
                                                        {"extensions", f.extensions},
                                                        {"write_support", f.write_support}}
                                     .dump()
                              << "\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// ListSerialPorts subcommand - the host's serial ports, for a configuration
// UI offering values to a parameter declared choices_from "serial-ports"
template<typename MachineType>
class ListSerialPortsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-serial-ports"; }
    std::string_view description() const override {
        return "List the host's serial ports";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-serial-ports\n"
                  << "\n"
                  << "Lists the host's serial ports, with the USB identity the OS reports\n"
                  << "for each (best-effort; a port may be missing or unopenable). These are\n"
                  << "the values a configuration UI offers for an extension parameter whose\n"
                  << "manifest declares choices_from \"serial-ports\" (see describe-extension).\n"
                  << "Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        const auto ports = beebium::serial::enumerate_serial_ports();
        auto hex = [](const std::optional<std::uint16_t>& id) -> std::string {
            if (!id) return {};
            char text[8];
            std::snprintf(text, sizeof text, "%04x", static_cast<unsigned>(*id));
            return text;
        };
        switch (resolve_output_format(global.output_format)) {
            case OutputFormat::Pretty:
                if (ports.empty()) {
                    std::cout << "No serial ports found.\n";
                }
                for (const auto& p : ports) {
                    std::cout << p.path;
                    if (p.usb_vendor_id) {
                        std::cout << "  [" << hex(p.usb_vendor_id) << ":"
                                  << hex(p.usb_product_id) << "]";
                    }
                    if (!p.manufacturer.empty() || !p.product.empty()) {
                        std::cout << "  " << p.manufacturer
                                  << (p.manufacturer.empty() || p.product.empty() ? "" : " ")
                                  << p.product;
                    }
                    std::cout << "\n";
                }
                break;
            case OutputFormat::Tsv:
                std::cout << "path\tusb_vendor_id\tusb_product_id\tmanufacturer\tproduct\t"
                             "serial_number\n";
                for (const auto& p : ports) {
                    std::cout << p.path << "\t" << hex(p.usb_vendor_id) << "\t"
                              << hex(p.usb_product_id) << "\t" << p.manufacturer << "\t"
                              << p.product << "\t" << p.serial_number << "\n";
                }
                break;
            case OutputFormat::Jsonl:
                for (const auto& p : ports) {
                    auto id = [](const std::optional<std::uint16_t>& v) {
                        return v ? nlohmann::ordered_json(*v) : nlohmann::ordered_json(nullptr);
                    };
                    std::cout << nlohmann::ordered_json{
                                     {"path", p.path},
                                     {"usb_vendor_id", id(p.usb_vendor_id)},
                                     {"usb_product_id", id(p.usb_product_id)},
                                     {"manufacturer", p.manufacturer},
                                     {"product", p.product},
                                     {"serial_number", p.serial_number}}
                                     .dump()
                              << "\n";
                }
                break;
            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// DescribeRom subcommand - parse a sideways ROM header and print it as JSON.
// Model-independent (ROM header parsing is generic). Used by frontends to show
// proper ROM titles/versions instead of filenames.
template<typename MachineType>
class DescribeRomSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-rom"; }
    std::string_view description() const override {
        return "Parse a sideways ROM image header and print it as JSON";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-rom <rom> [--rom-dir <dir>]\n"
                  << "\n"
                  << "Parses the standard sideways ROM header of <rom> (a ROM library name or a\n"
                  << "path to a ROM image) and prints it as JSON: whether it is a recognised\n"
                  << "sideways ROM, its title, version and copyright, the type-byte flags, and\n"
                  << "whether it is a language or a service ROM.\n"
                  << "\n"
                  << "  --rom-dir <dir>   Directory to resolve a bare ROM name against\n"
                  << "\n"
                  << "Output is always JSON regardless of the --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::string rom_ref;
        std::string rom_dir;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--rom-dir" && i + 1 < argc) {
                rom_dir = argv[++i];
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            } else if (rom_ref.empty()) {
                rom_ref = arg;
            } else {
                std::cerr << "Unexpected argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (rom_ref.empty()) {
            std::cerr << "Error: a ROM name or path is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (!rom_dir.empty()) {
            RomPaths::set_rom_directory(rom_dir);
        }

        std::vector<uint8_t> data;
        try {
            data = load_file(RomPaths::find_rom(rom_ref));
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::NOINPUT;
        }

        auto header = beebium::parse_sideways_rom_header(data);

        using ojson = nlohmann::ordered_json;
        ojson out;
        out["recognised"] = header.recognised;
        out["title"] = header.title;
        out["version"] = header.version;
        out["copyright"] = header.copyright;
        out["is_language"] = header.is_language();
        out["is_service_only"] = header.is_service_only();
        out["has_language_entry"] = header.has_language_entry;
        out["has_service_entry"] = header.has_service_entry;
        out["has_relocation_address"] = header.has_relocation_address;
        out["supports_electron_firmkeys"] = header.supports_electron_firmkeys;
        out["cpu_type"] = header.cpu_type;
        out["binary_version"] = header.binary_version;
        out["contains_romfs"] = header.contains_romfs;
        out["romfs_data_offset"] = header.romfs_data_offset;
        // "kinds" is a convenience list for UIs: ROMs can be any combination
        // of language / service / romfs (e.g. Acornsoft cartridges are all three).
        ojson kinds = ojson::array();
        if (header.has_language_entry) kinds.push_back("language");
        if (header.has_service_entry) kinds.push_back("service");
        if (header.contains_romfs) kinds.push_back("romfs");
        out["kinds"] = kinds;
        std::cout << out.dump(2) << "\n";
        return ExitCode::OK;
    }
};

// Validate and categorise a disc image, the way the running server does when
// a disc is inserted. This is the same rule -- the shared DiscFormatRegistry
// via load_disc_from_url_or_filepath -- reached through the CLI, so a front
// end configuring a machine before it is launched judges an image by exactly
// the criteria the machine itself will apply. There is no second, weaker rule
// anywhere.
template<typename MachineType>
class DescribeDiscImageSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-disc-image"; }
    std::string_view description() const override {
        return "Validate a floppy disc image and print its format as JSON";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-disc-image <path>\n"
                  << "\n"
                  << "Loads the disc image at <path> using the same format detection the\n"
                  << "server applies when a disc is inserted, and prints the result as JSON:\n"
                  << "whether it is a recognised disc image, its format, how many sides it\n"
                  << "has, and whether it is write-protected. When it is not recognised, the\n"
                  << "reason is given.\n"
                  << "\n"
                  << "Exit status is 0 whenever the file could be read, whether or not it is\n"
                  << "a disc image (read the \"recognised\" field); non-zero only if the file\n"
                  << "itself cannot be read.\n"
                  << "\n"
                  << "Output is always JSON regardless of the --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::string path;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            } else if (path.empty()) {
                path = arg;
            } else {
                std::cerr << "Unexpected argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (path.empty()) {
            std::cerr << "Error: a disc image path is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // A missing or unreadable file is an operational failure, not a
        // verdict on an image: report it on stderr with a non-zero status,
        // rather than as recognised=false.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            std::cerr << "Error: cannot read disc image: " << path << "\n";
            return ExitCode::NOINPUT;
        }

        // The one rule, shared with InsertDisc.
        auto result = load_disc_from_url_or_filepath(path);

        using ojson = nlohmann::ordered_json;
        ojson out;
        out["recognised"] = result.success();
        if (result.success()) {
            const Disc* disc = result.disc.get();
            out["format"] = std::string(disc->format_name());
            out["sides"] = disc->is_double_sided() ? 2 : 1;
            out["write_protected"] = disc->is_write_protected();
        } else {
            out["format"] = "";
            out["reason"] = result.error;
            // Machine-readable classification of the failure, so a front end can
            // choose a fitting label without parsing the reason text.
            out["kind"] = std::string(disc_load_error_kind_name(result.kind));
        }
        std::cout << out.dump(2) << "\n";
        return ExitCode::OK;
    }
};

} // namespace beebium::server

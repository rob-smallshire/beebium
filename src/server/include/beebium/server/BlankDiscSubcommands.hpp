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

// The subcommands for blank disc templates, building no machine:
// list-blank-discs lists the templates the server ships and create-disc copies
// one into place. DiscService's ListBlankDiscs and CreateDisc give clients the
// same.

#pragma once

#include "beebium/server/SubcommandCommon.hpp"
#include "beebium/server/SystemDirectories.hpp"
#include "beebium/disc/BlankDiscs.hpp"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace beebium::server {

// ListBlankDiscs subcommand - the blank disc templates create-disc copies
template<typename MachineType>
class ListBlankDiscsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-blank-discs"; }
    std::string_view description() const override {
        return "List the blank disc templates create-disc makes discs from";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-blank-discs\n"
                  << "\n"
                  << "Lists the blank disc templates shipped with the server, in file name\n"
                  << "order. Each has a label (its file name without the extension, which\n"
                  << "create-disc takes), its file name, its kind and its size in bytes.\n";
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

        const auto discs = beebium::list_blank_discs(find_blank_discs_dirpath());

        switch (resolve_output_format(global.output_format)) {
            case OutputFormat::Pretty:
                std::cout << "Blank disc templates:\n";
                for (const auto& disc : discs) {
                    std::cout << "  " << disc.label << "\n"
                              << "      File: " << disc.filename << "\n"
                              << "      Kind: " << disc.kind << ", " << disc.size_bytes
                              << " bytes\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "label\tfilename\tkind\tsize_bytes\n";
                for (const auto& disc : discs) {
                    std::cout << disc.label << "\t" << disc.filename << "\t" << disc.kind << "\t"
                              << disc.size_bytes << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& disc : discs) {
                    std::cout << nlohmann::ordered_json{{"label", disc.label},
                                                        {"filename", disc.filename},
                                                        {"kind", disc.kind},
                                                        {"size_bytes", disc.size_bytes}}
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

// CreateDisc subcommand - copy a blank disc template to a new disc image
template<typename MachineType>
class CreateDiscSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-disc"; }
    std::string_view description() const override {
        return "Create a blank disc image from a template";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " create-disc <label> <destination_filepath> [--overwrite]\n"
                  << "\n"
                  << "Copies the blank disc template with this label (see list-blank-discs)\n"
                  << "to the destination, byte for byte, and prints the path of the new disc.\n"
                  << "The new disc has the template's extension: a destination without one\n"
                  << "gets it, and one with another extension is refused. An existing\n"
                  << "destination is refused unless --overwrite is given.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::optional<std::string> label;
        std::optional<std::string> destination;
        bool overwrite = false;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            if (arg == "--overwrite") {
                overwrite = true;
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            } else if (!label) {
                label = std::string(arg);
            } else if (!destination) {
                destination = std::string(arg);
            } else {
                std::cerr << "Too many arguments\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }
        if (!label || !destination) {
            std::cerr << "Error: a label and a destination are required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        auto result = beebium::create_blank_disc(find_blank_discs_dirpath(), *label,
                                                 std::filesystem::path(*destination), overwrite);
        if (!result) {
            std::cerr << "Error: " << result.error << "\n";
            switch (result.kind) {
                case beebium::CreateBlankDiscErrorKind::UnknownLabel:
                    return ExitCode::NOINPUT;
                case beebium::CreateBlankDiscErrorKind::DestinationExists:
                    return ExitCode::CANTCREAT;
                case beebium::CreateBlankDiscErrorKind::WrongExtension:
                    return ExitCode::USAGE;
                default:
                    return ExitCode::IOERR;
            }
        }
        std::cout << result.created_filepath.string() << "\n";
        return ExitCode::OK;
    }
};

} // namespace beebium::server

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

// The subcommands for blank tape templates, building no machine:
// list-blank-tapes lists the templates the server ships and create-tape copies
// one into place, as list-blank-discs and create-disc do for discs.
// CassetteService's ListBlankTapes and CreateTape give clients the same.

#pragma once

#include "beebium/server/SubcommandCommon.hpp"
#include "beebium/server/SystemDirectories.hpp"
#include "beebium/cassette/BlankTapes.hpp"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace beebium::server {

// ListBlankTapes subcommand - the blank tape templates create-tape copies
template<typename MachineType>
class ListBlankTapesSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-blank-tapes"; }
    std::string_view description() const override {
        return "List the blank tape templates create-tape makes tapes from";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-blank-tapes\n"
                  << "\n"
                  << "Lists the blank tape templates shipped with the server, in file name\n"
                  << "order. Each has a label (its file name without the extension, which\n"
                  << "create-tape takes), its file name, its kind, its size in bytes and its\n"
                  << "length in seconds.\n";
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

        const auto tapes = beebium::list_blank_tapes(find_blank_tapes_dirpath());

        switch (resolve_output_format(global.output_format)) {
            case OutputFormat::Pretty:
                std::cout << "Blank tape templates:\n";
                for (const auto& tape : tapes) {
                    std::cout << "  " << tape.label << "\n"
                              << "      File: " << tape.filename << "\n"
                              << "      Kind: " << tape.kind << ", " << tape.size_bytes
                              << " bytes, " << tape.duration_seconds << " s\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "label\tfilename\tkind\tsize_bytes\tduration_seconds\n";
                for (const auto& tape : tapes) {
                    std::cout << tape.label << "\t" << tape.filename << "\t" << tape.kind << "\t"
                              << tape.size_bytes << "\t" << tape.duration_seconds << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& tape : tapes) {
                    std::cout << nlohmann::ordered_json{{"label", tape.label},
                                                        {"filename", tape.filename},
                                                        {"kind", tape.kind},
                                                        {"size_bytes", tape.size_bytes},
                                                        {"duration_seconds", tape.duration_seconds}}
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

// CreateTape subcommand - copy a blank tape template to a new tape image
template<typename MachineType>
class CreateTapeSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-tape"; }
    std::string_view description() const override {
        return "Create a blank tape image from a template";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " create-tape <label> <destination_filepath> [--overwrite]\n"
                  << "\n"
                  << "Copies the blank tape template with this label (see list-blank-tapes)\n"
                  << "to the destination, byte for byte, and prints the path of the new tape.\n"
                  << "The new tape has the template's extension: a destination without one\n"
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

        auto result = beebium::create_blank_tape(find_blank_tapes_dirpath(), *label,
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

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

// The subcommands on a saved machine that is not running: create-machine,
// which makes one from a preset; describe-machine, which says what one is and
// whether it is running; and reset-battery, which returns its battery-backed
// state to the seed. `start --machine` runs one.

#pragma once

#include "beebium/server/MachineDirectory.hpp"
#include "beebium/server/MachineDocument.hpp"
#include "beebium/server/MachineInstantiation.hpp"
#include "beebium/server/NvramStore.hpp"
#include "beebium/server/PresetSubcommands.hpp"
#include "beebium/server/PresetWriter.hpp"
#include "beebium/server/StartOptions.hpp"
#include "beebium/server/SubcommandCommon.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

namespace machine_subcommand_detail {

// The one positional argument of a subcommand on a saved machine, the
// directory or its document file, as the machine directory it names; or the
// exit code to return, having said why.
inline std::optional<std::filesystem::path> named_machine(int argc, char* argv[], int start,
                                                          const char* program_name,
                                                          void (*help)(const char*),
                                                          int& exit_code) {
    std::optional<std::string> named;
    for (int i = start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            help(program_name);
            exit_code = ExitCode::OK;
            return std::nullopt;
        }
        if ((!arg.empty() && arg[0] == '-') || named) {
            std::cerr << "Unexpected argument: " << arg << "\n";
            help(program_name);
            exit_code = ExitCode::USAGE;
            return std::nullopt;
        }
        named = std::string(arg);
    }
    if (!named) {
        std::cerr << "Error: a machine directory is required\n";
        help(program_name);
        exit_code = ExitCode::USAGE;
        return std::nullopt;
    }
    auto dirpath = machine_dirpath_named(*named);
    if (!dirpath) {
        std::cerr << "Error: " << *named << " is not a saved machine (no "
                  << machine_layout::RECORD_FILENAME << ")\n";
        exit_code = ExitCode::NOINPUT;
        return std::nullopt;
    }
    return std::filesystem::absolute(*dirpath);
}

// A describe-machine field as text and TSV show it: a string as it is,
// anything else as JSON.
inline std::string field_text(const nlohmann::ordered_json& value) {
    return value.is_string() ? value.get<std::string>() : value.dump();
}

}  // namespace machine_subcommand_detail

template<typename MachineType>
class CreateMachineSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-machine"; }
    std::string_view description() const override {
        return "Make a saved machine from a preset, without running it";
    }

    static void usage(const char* program_name) {
        std::cerr << "Usage: " << program_name
                  << " create-machine --output <dirpath> [--preset <id|path>] [options]\n"
                  << "\n"
                  << "Makes a saved machine at <dirpath>, which must not exist: the preset,\n"
                  << "with the machine options over it, unpacked into the directory as its\n"
                  << "preset.json beside every file the preset holds, and machine.json giving\n"
                  << "the machine a new identity. Run it with 'start --machine <dirpath>'.\n"
                  << "\n"
                  << "The machine owns the files it holds: the guest's writes to a disc image\n"
                  << "the preset shipped land in the machine's copy, never in the preset. A\n"
                  << "file the options name by its location (an absolute path) stays the\n"
                  << "user's own and is opened in place on every run.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --output <dirpath>    The machine directory to make (required)\n"
                  << "  --preset <id|path>    The preset to make it from (default: this model's\n"
                  << "                        defaults)\n"
                  << "  --machine-description <template>\n"
                  << "                        The machine's description (a template; default:\n"
                  << "                        the preset's, or the model's name)\n"
                  << "\n"
                  << "Any other machine option, as for 'start' and 'create-preset'; options that\n"
                  << "configure the launch rather than the machine are refused.\n"
                  << "\n"
                  << "Prints the machine directory. Exits " << ExitCode::CANTCREAT
                  << " when <dirpath> already exists.\n";
    }

    void help(const char* program_name) const override { usage(program_name); }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            usage(argv[0]);
            return ExitCode::OK;
        }

        std::string output;
        std::vector<std::string> machine_args{argv[0]};
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                usage(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--output" && i + 1 < argc) {
                output = argv[++i];
            } else {
                machine_args.push_back(std::move(arg));
            }
        }
        if (output.empty()) {
            std::cerr << "Error: --output is required\n";
            usage(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        {
            std::vector<char*> machine_argv;
            for (auto& a : machine_args) machine_argv.push_back(a.data());
            machine_argv.push_back(nullptr);
            if (auto exit_code = parse_start_arguments<MachineType>(
                    static_cast<int>(machine_args.size()), machine_argv.data(), 1, config,
                    StartOptionScope::MachineOnly)) {
                return *exit_code;
            }
        }
        if (auto err = validate_config(config)) {
            std::cerr << "Error: " << *err << "\n";
            return ExitCode::USAGE;
        }

        const auto dirpath = std::filesystem::absolute(output).lexically_normal();
        std::error_code ec;
        if (std::filesystem::exists(dirpath, ec)) {
            std::cerr << "Error: " << dirpath.string()
                      << " already exists; a machine is made in a new directory\n";
            return ExitCode::CANTCREAT;
        }

        // The machine's preset: what the configuration records, and the
        // files of the preset it was read from, thumbnail included (the
        // machine's first screen until it has one of its own).
        std::string error;
        const auto document = machine_preset_document(config, error);
        if (!document) {
            std::cerr << "Error: " << error << "\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto members = recorded_preset_members(config, *document, /*with_thumbnail=*/true, error,
                                               exit_code);
        if (!members) {
            std::cerr << "Error: " << error << "\n";
            return exit_code;
        }

        // Its identity, written with the rest, so the directory is a machine
        // whole or does not appear at all.
        const auto record = new_machine_record(config, generate_uuid_v4(), config.machine.machine_description);
        const auto record_text = machine_record_text(record);
        members->push_back({std::string(machine_layout::RECORD_FILENAME),
                            std::vector<std::uint8_t>(record_text.begin(), record_text.end())});
        if (const auto store_error = write_preset_directory(*members, dirpath);
            !store_error.empty()) {
            std::cerr << "Error: cannot make the machine: " << store_error << "\n";
            return ExitCode::IOERR;
        }

        // How it is shown: never read back, so a failure costs only its icon
        // and display name.
        try {
            write_machine_document_files(dirpath, record, machine_document_name(dirpath));
        } catch (const std::exception& e) {
            std::cerr << "Warning: cannot write the machine's document file and folder "
                         "metadata: "
                      << e.what() << "\n";
        }

        std::cout << dirpath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class DescribeMachineSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-machine"; }
    std::string_view description() const override {
        return "Describe a saved machine, and whether it is running";
    }

    static void usage(const char* program_name) {
        std::cerr << "Usage: " << program_name
                  << " describe-machine <dirpath|document filepath> [--format "
                     "pretty|tsv|jsonl]\n"
                  << "\n"
                  << "Describes the saved machine at <dirpath> (or whose document file is\n"
                  << "named), of any model:\n"
                  << "  machine_dirpath     the machine directory\n"
                  << "  name                its name: the directory's name without\n"
                  << "                      .beebium-machine (renaming the machine moves\n"
                  << "                      the directory)\n"
                  << "  description         its description (a template), absent when none\n"
                  << "  uuid                its identity, the same on every run\n"
                  << "  model               its model id\n"
                  << "  preset              the preset it was made from, if known\n"
                  << "  created, last_used  when it was made and last run (ISO 8601 UTC);\n"
                  << "                      last_used is absent until it has run\n"
                  << "  thumbnail_filepath  its last screen, absent until it has one\n"
                  << "  locked_by_pid       the process running it, absent when none is\n"
                  << "\n"
                  << "--format may also be given before the subcommand. Exits "
                  << ExitCode::NOINPUT << " when the\n"
                  << "path is not a saved machine, " << ExitCode::DATAERR
                  << " when its machine.json cannot be read.\n";
    }

    void help(const char* program_name) const override { usage(program_name); }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            usage(argv[0]);
            return ExitCode::OK;
        }

        // --format here as well as before the subcommand.
        OutputFormat format = global.output_format;
        std::vector<char*> rest{argv[0]};
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            std::optional<std::string> value;
            if (arg == "--format" && i + 1 < argc) {
                value = argv[++i];
            } else if (arg.rfind("--format=", 0) == 0) {
                value = std::string(arg.substr(9));
            } else {
                rest.push_back(argv[i]);
                continue;
            }
            try {
                format = parse_format_arg(*value);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        }

        int exit_code = ExitCode::OK;
        const auto dirpath = machine_subcommand_detail::named_machine(
            static_cast<int>(rest.size()), rest.data(), 1, argv[0], &usage, exit_code);
        if (!dirpath) return exit_code;

        std::string error;
        const auto record = read_machine_record(*dirpath, error);
        if (!record) {
            std::cerr << "Error: " << error << "\n";
            return ExitCode::DATAERR;
        }
        std::optional<std::string> holder;
        try {
            holder = machine_lock_holder(*dirpath);
        } catch (const std::exception& e) {
            std::cerr << "Error: cannot tell whether the machine is running: " << e.what()
                      << "\n";
            return ExitCode::IOERR;
        }

        nlohmann::ordered_json out;
        out["machine_dirpath"] = dirpath->string();
        out["name"] = machine_document_name(*dirpath);
        if (!record->description.empty()) out["description"] = record->description;
        out["uuid"] = record->uuid;
        out["model"] = record->model;
        if (!record->source_preset.empty()) out["preset"] = record->source_preset;
        out["created"] = record->created;
        if (!record->last_used.empty()) out["last_used"] = record->last_used;
        const auto thumbnail_filepath = *dirpath / machine_layout::THUMBNAIL_FILENAME;
        std::error_code ec;
        if (std::filesystem::is_regular_file(thumbnail_filepath, ec)) {
            out["thumbnail_filepath"] = thumbnail_filepath.string();
        }
        if (holder) {
            try {
                out["locked_by_pid"] = std::stoll(*holder);
            } catch (const std::exception&) {
                out["locked_by_pid"] = nullptr;  // running, by a process that cannot be named
            }
        }

        switch (resolve_output_format(format)) {
            case OutputFormat::Jsonl:
                std::cout << out.dump() << "\n";
                break;
            case OutputFormat::Tsv:
                for (const auto& [key, value] : out.items()) {
                    std::cout << key << "\t"
                              << machine_subcommand_detail::field_text(value)
                              << "\n";
                }
                break;
            default:
                for (const auto& [key, value] : out.items()) {
                    std::cout << key << ": "
                              << machine_subcommand_detail::field_text(value)
                              << "\n";
                }
                break;
        }
        return ExitCode::OK;
    }
};

template<typename MachineType>
class ResetBatterySubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "reset-battery"; }
    std::string_view description() const override {
        return "Return a saved machine's battery-backed state to the seed";
    }

    static void usage(const char* program_name) {
        std::cerr << "Usage: " << program_name
                  << " reset-battery <dirpath|document filepath>\n"
                  << "\n"
                  << "Forgets everything the battery-backed units of the saved machine at\n"
                  << "<dirpath> kept (board RAM, a clock's CMOS RAM and time, an extension's\n"
                  << "clock), as taking out the battery does: at its next run each starts\n"
                  << "from the contents of a newly set up board. The machine must not be\n"
                  << "running; exits "
                  << ExitCode::TEMPFAIL << ",\n"
                  << "naming the process, when it is.\n";
    }

    void help(const char* program_name) const override { usage(program_name); }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            usage(argv[0]);
            return ExitCode::OK;
        }
        int exit_code = ExitCode::OK;
        const auto dirpath = machine_subcommand_detail::named_machine(
            argc, argv, global.subcommand_argv_start, argv[0], &usage, exit_code);
        if (!dirpath) return exit_code;

        try {
            // Held while forgetting, so no run starts meanwhile.
            const auto directory = MachineDirectory::open(*dirpath);
            for (const auto& battery_dirpath : battery_dirpaths(*dirpath)) {
                forget_battery_units(battery_dirpath);
            }
        } catch (const MachineInUse& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::TEMPFAIL;
        } catch (const std::exception& e) {
            std::cerr << "Error: cannot reset the battery: " << e.what() << "\n";
            return ExitCode::IOERR;
        }
        return ExitCode::OK;
    }
};

}  // namespace beebium::server

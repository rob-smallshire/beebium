// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVER_SERVER_MAIN_HPP
#define BEEBIUM_SERVER_SERVER_MAIN_HPP

// The server executable's entry point: server_main parses the global options
// and dispatches to a subcommand. Each family of subcommands lives in its own
// header; this one only gathers them.

#include "beebium/server/AunMapSubcommands.hpp"
#include "beebium/server/ExtensionSubcommands.hpp"
#include "beebium/server/LaunchSubcommands.hpp"
#include "beebium/server/ListingSubcommands.hpp"
#include "beebium/server/MachineSubcommands.hpp"
#include "beebium/server/PresetSubcommands.hpp"
#include "beebium/server/SubcommandCommon.hpp"

#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace beebium::server {

// Help subcommand - shows help for other subcommands
template<typename MachineType>
class HelpSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "help"; }
    std::string_view description() const override { return "Show help for a subcommand"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " help [subcommand]\n"
                  << "\n"
                  << "Shows help for the specified subcommand, or global help if none specified.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override;  // Defined after get_subcommands
};

// ============================================================================
// Subcommand registry and dispatch
// ============================================================================

// Get the singleton registry of all subcommands
template<typename MachineType>
const std::vector<std::unique_ptr<Subcommand<MachineType>>>& get_subcommands() {
    static auto cmds = []() {
        std::vector<std::unique_ptr<Subcommand<MachineType>>> v;
        v.push_back(std::make_unique<StartSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListFdcsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListDiscFormatsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListExtensionsSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeExtensionSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListAttachmentPointsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListSerialPortsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListDescriptionPlaceholdersSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribePresetSchemaSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeRomSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeDiscImageSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListPresetsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ShowPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ResolvePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<CreatePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<DeletePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ImportPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ExportPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<CreateMachineSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeMachineSubcommand<MachineType>>());
        v.push_back(std::make_unique<ResetBatterySubcommand<MachineType>>());
        v.push_back(std::make_unique<ReportAunMapFilepathSubcommand<MachineType>>());
        v.push_back(std::make_unique<CreateAunMapSubcommand<MachineType>>());
        v.push_back(std::make_unique<ShowAunMapSubcommand<MachineType>>());
        v.push_back(std::make_unique<AddAunPeerSubcommand<MachineType>>());
        v.push_back(std::make_unique<RemoveAunPeerSubcommand<MachineType>>());
        v.push_back(std::make_unique<AddAunSubnetSubcommand<MachineType>>());
        v.push_back(std::make_unique<RemoveAunSubnetSubcommand<MachineType>>());
        v.push_back(std::make_unique<CaptureScreenshotSubcommand<MachineType>>());
        v.push_back(std::make_unique<HelpSubcommand<MachineType>>());
        return v;
    }();
    return cmds;
}

// HelpSubcommand::invoke implementation (needs get_subcommands)
template<typename MachineType>
int HelpSubcommand<MachineType>::invoke(int argc, char* argv[], const GlobalConfig& global) const {
    // Check for target subcommand name in arguments
    std::string target_name;
    for (int i = global.subcommand_argv_start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            // help --help shows help for the help command
            help(argv[0]);
            return ExitCode::OK;
        }
        if (!arg.empty() && arg[0] != '-') {
            target_name = std::string(arg);
            break;
        }
    }

    if (target_name.empty()) {
        // No target: show global help
        print_global_help<MachineType>(argv[0]);
        return ExitCode::OK;
    }

    // Find and show help for the target subcommand
    for (const auto& cmd : get_subcommands<MachineType>()) {
        if (cmd->name() == target_name) {
            cmd->help(argv[0]);
            return ExitCode::OK;
        }
    }

    std::cerr << "Unknown subcommand: " << target_name << "\n";
    print_global_help<MachineType>(argv[0]);
    return ExitCode::USAGE;
}

// Print global help (lists all subcommands)
template<typename MachineType>
void print_global_help(const char* program_name) {
    using Memory = typename MachineType::Memory;

    std::cerr << "Usage: " << program_name << " [global-options] <subcommand> [subcommand-options]\n"
              << "\n"
              << "Beebium " << BEEBIUM_VERSION << " - " << Memory::MACHINE_DISPLAY_NAME << " emulator server\n"
              << "\n"
              << "Global options:\n"
              << "  --help, -h          Show this help message\n"
              << "  --format <format>   Output format for data commands:\n"
              << "                      pretty - human-friendly (default if TTY)\n"
              << "                      tsv    - tab-separated values (default if not TTY)\n"
              << "                      jsonl  - JSON Lines (one JSON object per line)\n"
              << "\n"
              << "Subcommands:\n";

    for (const auto& cmd : get_subcommands<MachineType>()) {
        std::cerr << "  " << cmd->name();
        // Pad to align descriptions
        for (size_t i = cmd->name().length(); i < 22; ++i) {
            std::cerr << ' ';
        }
        std::cerr << cmd->description() << "\n";
    }

    std::cerr << "\n"
              << "Use '" << program_name << " <subcommand> --help' for subcommand options.\n"
              << "Use '" << program_name << " help <subcommand>' for subcommand help.\n"
              << "\n"
              << "If no subcommand is specified, 'start' is assumed.\n";
}

// Dispatch to the appropriate subcommand
template<typename MachineType>
int dispatch_subcommand(int argc, char* argv[], const GlobalConfig& global) {
    // Determine subcommand name (default to "start")
    std::string subcommand_name = global.subcommand_name;
    if (subcommand_name.empty()) {
        // No subcommand specified
        if (global.help_requested) {
            // Global --help with no subcommand
            print_global_help<MachineType>(argv[0]);
            return ExitCode::OK;
        }
        subcommand_name = "start";  // Default subcommand
    }

    // Find and invoke the subcommand
    for (const auto& cmd : get_subcommands<MachineType>()) {
        if (cmd->name() == subcommand_name) {
            return cmd->invoke(argc, argv, global);
        }
    }

    // Unknown subcommand
    std::cerr << "Unknown subcommand: " << subcommand_name << "\n";
    print_global_help<MachineType>(argv[0]);
    return ExitCode::USAGE;
}

// ============================================================================
// Main entry point
// ============================================================================

template<typename MachineType>
int server_main(int argc, char* argv[]) {
    // Parse global arguments
    GlobalConfig global;
    if (auto exit_code = parse_global_arguments(argc, argv, global)) {
        return *exit_code;
    }

    // Dispatch to appropriate subcommand
    return dispatch_subcommand<MachineType>(argc, argv, global);
}

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SERVER_MAIN_HPP


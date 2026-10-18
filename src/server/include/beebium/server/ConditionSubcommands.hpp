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

#ifndef BEEBIUM_SERVER_CONDITION_SUBCOMMANDS_HPP
#define BEEBIUM_SERVER_CONDITION_SUBCOMMANDS_HPP

#include "beebium/DebugVariable.hpp"
#include "beebium/debugger/Expression.hpp"
#include "beebium/server/SubcommandCommon.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// describe-conditions: every name a breakpoint or watchpoint condition can use
// on this model, as GetCpuDescriptor lists them for the running machine.
template<typename MachineType>
class DescribeConditionsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-conditions"; }
    std::string_view description() const override {
        return "List the names a breakpoint or watchpoint condition can use";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-conditions\n"
                  << "\n"
                  << "Lists every name a debugger breakpoint or watchpoint condition can use\n"
                  << "on this model, with what it means and where it applies:\n"
                  << "  built_in    on every CPU: registers, flags, cycles, hits\n"
                  << "  machine     this model's latches, read as the hardware holds them\n"
                  << "  watchpoint  the access a watchpoint judges; watchpoint conditions only\n"
                  << "\n"
                  << "The same list a running server gives in GetCpuDescriptor. romsel & 0x80\n"
                  << "reads ROMSEL bit 7 on every model; its meaning is the model's.\n"
                  << "\n"
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

        struct Row {
            std::string name;
            std::string scope;
            std::string description;
        };
        std::vector<Row> rows;
        for (const auto& built_in : expression_built_ins()) {
            rows.push_back({std::string(built_in.name), "built_in",
                            std::string(built_in.description)});
        }
        const auto machine = std::make_unique<MachineType>();
        for (const auto& variable : machine->debug_variables()) {
            rows.push_back({std::string(variable.name),
                            variable.watchpoint_only ? "watchpoint" : "machine",
                            std::string(variable.description)});
        }
        for (const auto& access : WATCHPOINT_ACCESS_VARIABLES) {
            rows.push_back({std::string(access.name), "watchpoint",
                            std::string(access.description)});
        }

        switch (resolve_output_format(global.output_format)) {
            case OutputFormat::Pretty: {
                std::string scope;
                for (const auto& row : rows) {
                    if (row.scope != scope) {
                        scope = row.scope;
                        std::cout << (scope == "built_in" ? "On every CPU:\n"
                                      : scope == "machine" ? "This model's:\n"
                                                           : "Watchpoint conditions only:\n");
                    }
                    std::cout << "  " << row.name << "\n      " << row.description << "\n";
                }
                break;
            }
            case OutputFormat::Tsv:
                std::cout << "name\tscope\tdescription\n";
                for (const auto& row : rows) {
                    std::cout << row.name << "\t" << row.scope << "\t" << row.description << "\n";
                }
                break;
            case OutputFormat::Jsonl:
                for (const auto& row : rows) {
                    std::cout << nlohmann::ordered_json{{"name", row.name},
                                                        {"scope", row.scope},
                                                        {"description", row.description}}
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

}  // namespace beebium::server

#endif  // BEEBIUM_SERVER_CONDITION_SUBCOMMANDS_HPP

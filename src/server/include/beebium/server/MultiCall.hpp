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

// The one server executable, beebium-server, runs every machine model: it is a
// multi-call binary (docs/deployment.md, "One executable"). Invoked by a
// model's name (beebium-master-128, a link to it, or a Windows launcher that
// passes its own command line on) it runs that model with argv unchanged.
// Invoked by any other name, beebium-server among them, its first argument
// names the model (beebium-server master-128 start ...), and the model sees
// the argv it would have seen invoked by its own name: argv[0] named after the
// model, the rest shifted down by one.

#pragma once

#include "beebium/MachineModels.hpp"
#include "beebium/PlatformUtils.hpp"
#include "beebium/server/SubcommandCommon.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace beebium::server {

// A model the executable runs, and the entry point that runs it.
struct ServerModel {
    std::string_view id;
    int (*run)(int argc, char* argv[]);
};

// The prefix every model's executable name carries: beebium-<model id>.
inline constexpr std::string_view kModelExecutablePrefix = "beebium-";

// The executable's own name, beebium-server.
inline constexpr std::string_view kMultiCallName = "beebium-server";

namespace detail {

inline bool is_separator(char c) {
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

inline bool ends_with_exe(std::string_view name) {
    if (name.size() < 4) return false;
    std::string tail(name.substr(name.size() - 4));
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".exe";
}

}  // namespace detail

// The name `argv0` invokes: its last path component, less a Windows ".exe"
// (in any case).
inline std::string invoked_name(std::string_view argv0) {
    size_t start = 0;
    for (size_t i = 0; i < argv0.size(); ++i) {
        if (detail::is_separator(argv0[i])) start = i + 1;
    }
    std::string name(argv0.substr(start));
#ifdef _WIN32
    if (detail::ends_with_exe(name)) name.resize(name.size() - 4);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return name;
}

inline const ServerModel* find_server_model(std::span<const ServerModel> models, std::string_view id) {
    for (const auto& model : models) {
        if (model.id == id) return &model;
    }
    return nullptr;
}

// What an invocation runs: the model, and the argv it sees (pointing into the
// original argv, but for argv[0] when the first argument named the model).
struct Dispatch {
    const ServerModel* model = nullptr;
    bool by_argument = false;  // the first argument named the model
    // argv[0] for a model named by argument, on the heap so that argv's
    // pointer to it survives the Dispatch being moved.
    std::unique_ptr<std::string> argv0;
    std::vector<char*> argv;   // null-terminated

    int argc() const { return static_cast<int>(argv.size()) - 1; }
};

// The model an invocation names, by its name or else by its first argument;
// nullopt when it names none.
inline std::optional<Dispatch> dispatch(std::span<const ServerModel> models, int argc, char* argv[]) {
    if (argc < 1 || argv[0] == nullptr) return std::nullopt;
    const std::string name = invoked_name(argv[0]);
    if (name.starts_with(kModelExecutablePrefix)) {
        if (const auto* model = find_server_model(models, std::string_view(name).substr(kModelExecutablePrefix.size()))) {
            Dispatch result;
            result.model = model;
            result.argv.assign(argv, argv + argc);
            result.argv.push_back(nullptr);
            return result;
        }
    }
    if (argc < 2) return std::nullopt;
    const auto* model = find_server_model(models, argv[1]);
    if (!model) return std::nullopt;

    Dispatch result;
    result.model = model;
    result.by_argument = true;
    const std::string_view argv0 = argv[0];
    size_t start = 0;
    for (size_t i = 0; i < argv0.size(); ++i) {
        if (detail::is_separator(argv0[i])) start = i + 1;
    }
    result.argv0 = std::make_unique<std::string>(std::string(argv0.substr(0, start)) +
                                                 std::string(kModelExecutablePrefix) + std::string(model->id));
    if (detail::ends_with_exe(argv0)) *result.argv0 += argv0.substr(argv0.size() - 4);
    result.argv.push_back(result.argv0->data());
    for (int i = 2; i < argc; ++i) result.argv.push_back(argv[i]);
    result.argv.push_back(nullptr);
    return result;
}

// The path an invocation by name was made through, for SystemInfo to report:
// argv[0] made absolute (without resolving links) when it holds a directory,
// else the first match for it on PATH (with ".exe" on Windows), else nullopt.
inline std::optional<std::filesystem::path> invoked_path(std::string_view argv0) {
    if (argv0.empty()) return std::nullopt;
    std::error_code ec;
    if (std::any_of(argv0.begin(), argv0.end(), detail::is_separator)) {
        auto absolute = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (ec) return std::nullopt;
        return absolute.lexically_normal();
    }
    const char* path_env = std::getenv("PATH");
    if (!path_env) return std::nullopt;
    std::string name(argv0);
#ifdef _WIN32
    constexpr char list_separator = ';';
    if (std::filesystem::path(name).extension().empty()) name += ".exe";
#else
    constexpr char list_separator = ':';
#endif
    const std::string_view entries = path_env;
    size_t start = 0;
    while (start <= entries.size()) {
        size_t end = entries.find(list_separator, start);
        if (end == std::string_view::npos) end = entries.size();
        const std::string_view entry = entries.substr(start, end - start);
        if (!entry.empty()) {
            const auto candidate = std::filesystem::path(entry) / name;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                auto absolute = std::filesystem::absolute(candidate, ec);
                if (!ec) return absolute.lexically_normal();
            }
        }
        start = end + 1;
    }
    return std::nullopt;
}

inline void print_multi_call_help(std::ostream& out, std::span<const ServerModel> models, const char* version) {
    out << "Usage: " << kMultiCallName << " <model> [global-options] <subcommand> [subcommand-options]\n"
        << "\n"
        << "Beebium " << version << " - BBC Micro emulator server\n"
        << "\n"
        << "Runs the machine model the first argument names, as beebium-<model> would.\n"
        << "Invoked as beebium-<model> (a link to this executable), it runs that model.\n"
        << "\n"
        << "Models:\n";
    for (const auto& model : models) {
        out << "  " << model.id;
        for (size_t i = model.id.size(); i < 26; ++i) out << ' ';
        const auto* entry = machine_models::find(model.id);
        out << (entry ? entry->display_name : std::string_view{}) << "\n";
    }
    out << "\n"
        << "Use '" << kMultiCallName << " <model> --help' for the model's subcommands.\n";
}

// The executable's main: dispatch on the invoked name or the first argument,
// else answer --help and --version, else report a usage error.
inline int multi_call_main(std::span<const ServerModel> models, int argc, char* argv[], const char* version,
                           std::ostream& out, std::ostream& err) {
    if (auto chosen = dispatch(models, argc, argv)) {
        if (!chosen->by_argument) {
            if (auto path = invoked_path(argv[0])) beebium::platform::set_invoked_executable_path(*path);
        }
        return chosen->model->run(chosen->argc(), chosen->argv.data());
    }
    const std::string_view first = argc >= 2 && argv[1] ? argv[1] : "";
    if (first == "--help" || first == "-h" || first == "help") {
        print_multi_call_help(out, models, version);
        return ExitCode::OK;
    }
    if (first == "--version") {
        out << kMultiCallName << " " << version << "\n";
        return ExitCode::OK;
    }
    if (first.empty()) {
        err << kMultiCallName << ": name the machine model to run.\n\n";
    } else {
        err << kMultiCallName << ": there is no machine model '" << first << "'.\n\n";
    }
    print_multi_call_help(err, models, version);
    return ExitCode::USAGE;
}

}  // namespace beebium::server

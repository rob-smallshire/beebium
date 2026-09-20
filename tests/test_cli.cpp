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

// test_cli.cpp
// Tests for CLI subcommand architecture (parse_global_arguments, dispatch_subcommand)

#include <beebium/server/ServerMain.hpp>
#include <beebium/Machines.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace beebium::server;
using MachineType = beebium::ModelB;

// Helper to convert string vector to argc/argv format
struct ArgvHelper {
    std::vector<std::string> args;
    std::vector<char*> argv;

    explicit ArgvHelper(std::initializer_list<const char*> init) {
        for (const char* arg : init) {
            args.emplace_back(arg);
        }
        for (auto& s : args) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);
    }

    int argc() const { return static_cast<int>(args.size()); }
    char** data() { return argv.data(); }
};

// ============================================================================
// parse_global_arguments() tests
// ============================================================================

TEST_CASE("parse_global_arguments: no arguments returns empty subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name.empty());
    REQUIRE(global.subcommand_argv_start == 1);
    REQUIRE(global.help_requested == false);
}

TEST_CASE("parse_global_arguments: --help sets help_requested", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "--help"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.help_requested == true);
}

TEST_CASE("parse_global_arguments: -h sets help_requested", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "-h"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.help_requested == true);
}

TEST_CASE("parse_global_arguments: subcommand name is recognized", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "start"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "start");
    REQUIRE(global.subcommand_argv_start == 2);
}

TEST_CASE("parse_global_arguments: subcommand with options", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "start", "--port", "8080"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "start");
    REQUIRE(global.subcommand_argv_start == 2);
}

TEST_CASE("parse_global_arguments: --help before subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "--help", "start"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.help_requested == true);
    REQUIRE(global.subcommand_name == "start");
}

TEST_CASE("parse_global_arguments: list-fdcs subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: describe-machine subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "describe-machine");
}

TEST_CASE("parse_global_arguments: help subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "help"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "help");
}

TEST_CASE("parse_global_arguments: describe-preset-schema subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "describe-preset-schema"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "describe-preset-schema");
}

TEST_CASE("parse_global_arguments: unknown option passes through to default subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "--unknown-global"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    // Unknown options are NOT errors at the global level - they pass through
    // to the default "start" subcommand which will handle them
    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name.empty());  // No explicit subcommand
    REQUIRE(global.subcommand_argv_start == 1);  // Options start at index 1
}

// ============================================================================
// parse_start_arguments() tests
// ============================================================================

TEST_CASE("parse_start_arguments: --help returns OK", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--help"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::OK);
}

TEST_CASE("parse_start_arguments: --port sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--port", "8080"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 8080);
}

TEST_CASE("parse_start_arguments: --port accepts hex value", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--port", "0xbeeb"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 48875);  // 0xBEEB
}

TEST_CASE("parse_start_arguments: --mos sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--mos", "custom.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.mos_filepath == "custom.rom");
}

TEST_CASE("parse_start_arguments: --screen-mode sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--screen-mode", "4"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.screen_mode == 4);
    REQUIRE(config.screen_mode_set == true);
}

TEST_CASE("parse_start_arguments: --screen-mode accepts binary value", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--screen-mode", "0b101"};  // Binary 5
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.screen_mode == 5);
    REQUIRE(config.screen_mode_set == true);
}

TEST_CASE("parse_start_arguments: --screen-mode out of range returns USAGE", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--screen-mode", "8"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --links sets config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--links", "128"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.raw_links == 128);
}

TEST_CASE("parse_start_arguments: --links accepts hex value", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--links", "0xff"};  // Hex 255
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.raw_links == 255);
}

TEST_CASE("parse_start_arguments: --links out of range returns USAGE", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--links", "256"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: unknown argument returns USAGE", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--unknown"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: multiple options combined", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--mos", "mos.rom", "--port", "9000", "--screen-mode", "0"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.mos_filepath == "mos.rom");
    REQUIRE(config.port == 9000);
    REQUIRE(config.screen_mode == 0);
}

TEST_CASE("parse_start_arguments: implicit start (no subcommand)", "[cli][parse_start_arguments]") {
    // When no subcommand is given, start_index is 1 (first arg after program name)
    ArgvHelper args{"beebium", "--port", "7000"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 1, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.port == 7000);
}

// ============================================================================
// Econet transport selection
// ============================================================================
//
// Both AUN (built-in) and Piconet (plugin) are now extension instances
// dispatched via the generic --<extension-name> machinery. There are
// no transport-specific fields on ServerConfig any more; the parser
// tests below check that the relevant ExtensionInstance is constructed
// with the right config map.

// ============================================================================
// dispatch_subcommand() tests
// ============================================================================

TEST_CASE("dispatch_subcommand: list-fdcs returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "list-fdcs"};
    GlobalConfig global;
    global.subcommand_name = "list-fdcs";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: list-floppy-formats returns OK", "[cli][dispatch_subcommand][list-floppy-formats]") {
    ArgvHelper args{"beebium", "list-floppy-formats"};
    GlobalConfig global;
    global.subcommand_name = "list-floppy-formats";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: list-floppy-formats --help returns OK", "[cli][dispatch_subcommand][list-floppy-formats]") {
    ArgvHelper args{"beebium", "list-floppy-formats", "--help"};
    GlobalConfig global;
    global.subcommand_name = "list-floppy-formats";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: list-floppy-formats with unknown arg returns USAGE", "[cli][dispatch_subcommand][list-floppy-formats]") {
    ArgvHelper args{"beebium", "list-floppy-formats", "--bogus"};
    GlobalConfig global;
    global.subcommand_name = "list-floppy-formats";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: list-floppy-formats output contains all registered formats", "[cli][dispatch_subcommand][list-floppy-formats]") {
    ArgvHelper args{"beebium", "--format", "tsv", "list-floppy-formats"};
    GlobalConfig global;
    global.subcommand_name = "list-floppy-formats";
    global.subcommand_argv_start = 4;
    global.output_format = OutputFormat::Tsv;

    // Capture stdout
    std::ostringstream capture;
    auto* old_buf = std::cout.rdbuf(capture.rdbuf());

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    std::cout.rdbuf(old_buf);

    REQUIRE(result == ExitCode::OK);

    std::string output = capture.str();
    // Header row
    CHECK(output.find("name\t") != std::string::npos);
    // All three registered format handlers should appear
    CHECK(output.find("SSD/DSD") != std::string::npos);
    CHECK(output.find("ADFS") != std::string::npos);
    CHECK(output.find("HFE") != std::string::npos);
    // Extensions should be present
    CHECK(output.find(".ssd") != std::string::npos);
    CHECK(output.find(".adl") != std::string::npos);
    CHECK(output.find(".hfe") != std::string::npos);
}

TEST_CASE("parse_global_arguments: list-floppy-formats subcommand", "[cli][parse_global_arguments]") {
    ArgvHelper args{"beebium", "list-floppy-formats"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "list-floppy-formats");
}

TEST_CASE("dispatch_subcommand: describe-machine returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "describe-machine"};
    GlobalConfig global;
    global.subcommand_name = "describe-machine";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: help returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "help"};
    GlobalConfig global;
    global.subcommand_name = "help";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: describe-preset-schema returns OK", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "describe-preset-schema"};
    GlobalConfig global;
    global.subcommand_name = "describe-preset-schema";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: unknown subcommand returns USAGE", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "unknown-command"};
    GlobalConfig global;
    global.subcommand_name = "unknown-command";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: global --help with no subcommand shows global help", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "--help"};
    GlobalConfig global;
    global.help_requested = true;
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: help with target subcommand", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "help", "list-fdcs"};
    GlobalConfig global;
    global.subcommand_name = "help";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: help with unknown target returns USAGE", "[cli][dispatch_subcommand]") {
    ArgvHelper args{"beebium", "help", "unknown-cmd"};
    GlobalConfig global;
    global.subcommand_name = "help";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

// ============================================================================
// ExitCode constants tests
// ============================================================================

TEST_CASE("ExitCode: constants have expected values", "[cli][ExitCode]") {
    REQUIRE(ExitCode::OK == 0);
    REQUIRE(ExitCode::USAGE == 64);
    REQUIRE(ExitCode::DATAERR == 65);
    REQUIRE(ExitCode::NOINPUT == 66);
    REQUIRE(ExitCode::SOFTWARE == 70);
    REQUIRE(ExitCode::IOERR == 74);
    REQUIRE(ExitCode::CONFIG == 78);
}

// ============================================================================
// GlobalConfig tests
// ============================================================================

TEST_CASE("GlobalConfig: default values", "[cli][GlobalConfig]") {
    GlobalConfig global;

    REQUIRE(global.subcommand_name.empty());
    REQUIRE(global.subcommand_argv_start == 1);
    REQUIRE(global.help_requested == false);
}

// ============================================================================
// get_subcommands() tests
// ============================================================================

TEST_CASE("get_subcommands: contains expected subcommands", "[cli][get_subcommands]") {
    const auto& cmds = get_subcommands<MachineType>();

    std::vector<std::string> names;
    for (const auto& cmd : cmds) {
        names.push_back(std::string(cmd->name()));
    }

    REQUIRE(std::find(names.begin(), names.end(), "start") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "list-fdcs") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "describe-machine") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "describe-preset-schema") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "list-presets") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "show-preset") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "report-presets-dirpath") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "create-preset") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "delete-preset") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "import-preset") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "export-preset") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "capture-screenshot") != names.end());
    REQUIRE(std::find(names.begin(), names.end(), "help") != names.end());
}

TEST_CASE("get_subcommands: subcommands have non-empty descriptions", "[cli][get_subcommands]") {
    const auto& cmds = get_subcommands<MachineType>();

    for (const auto& cmd : cmds) {
        REQUIRE_FALSE(cmd->description().empty());
    }
}

// ============================================================================
// parse_int() tests
// ============================================================================

TEST_CASE("parse_int: decimal integers", "[cli][parse_int]") {
    REQUIRE(parse_int("0") == 0);
    REQUIRE(parse_int("1") == 1);
    REQUIRE(parse_int("123") == 123);
    REQUIRE(parse_int("255") == 255);
    REQUIRE(parse_int("48875") == 48875);
    REQUIRE(parse_int("2147483647") == 2147483647);  // INT_MAX
}

TEST_CASE("parse_int: negative decimal integers", "[cli][parse_int]") {
    REQUIRE(parse_int("-1") == -1);
    REQUIRE(parse_int("-123") == -123);
    REQUIRE(parse_int("-2147483648") == -2147483648);  // INT_MIN
}

TEST_CASE("parse_int: binary integers (0b prefix)", "[cli][parse_int]") {
    REQUIRE(parse_int("0b0") == 0);
    REQUIRE(parse_int("0b1") == 1);
    REQUIRE(parse_int("0b10") == 2);
    REQUIRE(parse_int("0b1010") == 10);
    REQUIRE(parse_int("0b11111111") == 255);
    REQUIRE(parse_int("0b1111") == 15);
}

TEST_CASE("parse_int: binary integers (0B prefix, uppercase)", "[cli][parse_int]") {
    REQUIRE(parse_int("0B1010") == 10);
    REQUIRE(parse_int("0B11111111") == 255);
}

TEST_CASE("parse_int: octal integers (0o prefix)", "[cli][parse_int]") {
    REQUIRE(parse_int("0o0") == 0);
    REQUIRE(parse_int("0o1") == 1);
    REQUIRE(parse_int("0o7") == 7);
    REQUIRE(parse_int("0o10") == 8);
    REQUIRE(parse_int("0o17") == 15);
    REQUIRE(parse_int("0o377") == 255);
}

TEST_CASE("parse_int: octal integers (0O prefix, uppercase)", "[cli][parse_int]") {
    REQUIRE(parse_int("0O17") == 15);
    REQUIRE(parse_int("0O377") == 255);
}

TEST_CASE("parse_int: hexadecimal integers (0x prefix)", "[cli][parse_int]") {
    REQUIRE(parse_int("0x0") == 0);
    REQUIRE(parse_int("0x1") == 1);
    REQUIRE(parse_int("0xf") == 15);
    REQUIRE(parse_int("0xff") == 255);
    REQUIRE(parse_int("0xbeeb") == 48875);
    REQUIRE(parse_int("0xBEEB") == 48875);
    REQUIRE(parse_int("0x7fffffff") == 2147483647);  // INT_MAX in hex
}

TEST_CASE("parse_int: hexadecimal integers (0X prefix, uppercase)", "[cli][parse_int]") {
    REQUIRE(parse_int("0Xff") == 255);
    REQUIRE(parse_int("0XBEEB") == 48875);
}

TEST_CASE("parse_int: empty string throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int(""), std::runtime_error);
}

TEST_CASE("parse_int: invalid format throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int("abc"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("12abc"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("0b2"), std::runtime_error);  // Invalid binary digit
    REQUIRE_THROWS_AS(parse_int("0o8"), std::runtime_error);  // Invalid octal digit
    REQUIRE_THROWS_AS(parse_int("0xgg"), std::runtime_error); // Invalid hex digit
}

TEST_CASE("parse_int: prefix without digits throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int("0b"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("0o"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_int("0x"), std::runtime_error);
}

TEST_CASE("parse_int: overflow throws", "[cli][parse_int]") {
    REQUIRE_THROWS_AS(parse_int("0xDEADBEEF"), std::runtime_error);  // > INT_MAX
    REQUIRE_THROWS_AS(parse_int("9999999999"), std::runtime_error);  // > INT_MAX
}

TEST_CASE("parse_int: context appears in error message", "[cli][parse_int]") {
    try {
        parse_int("invalid", "--port");
        FAIL("Expected exception");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        REQUIRE(msg.find("--port") != std::string::npos);
    }
}

// ============================================================================
// OutputFormat and --format option tests
// ============================================================================

TEST_CASE("parse_global_arguments: default output_format is Auto", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Auto);
}

TEST_CASE("parse_global_arguments: --format pretty sets output_format", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "pretty", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Pretty);
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: --format tsv sets output_format", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "tsv", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Tsv);
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: --format jsonl sets output_format", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "jsonl", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Jsonl);
    REQUIRE(global.subcommand_name == "list-fdcs");
}

TEST_CASE("parse_global_arguments: --format=pretty works", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=pretty", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Pretty);
    REQUIRE(global.subcommand_name == "describe-machine");
}

TEST_CASE("parse_global_arguments: --format=tsv works", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=tsv", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Tsv);
}

TEST_CASE("parse_global_arguments: --format=jsonl works", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=jsonl", "describe-machine"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.output_format == OutputFormat::Jsonl);
}

TEST_CASE("parse_global_arguments: invalid --format returns USAGE", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format", "invalid", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_global_arguments: invalid --format=value returns USAGE", "[cli][parse_global_arguments][format]") {
    ArgvHelper args{"beebium", "--format=xml", "list-fdcs"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("resolve_output_format: Pretty returns Pretty", "[cli][resolve_output_format]") {
    REQUIRE(resolve_output_format(OutputFormat::Pretty) == OutputFormat::Pretty);
}

TEST_CASE("resolve_output_format: Tsv returns Tsv", "[cli][resolve_output_format]") {
    REQUIRE(resolve_output_format(OutputFormat::Tsv) == OutputFormat::Tsv);
}

TEST_CASE("resolve_output_format: Jsonl returns Jsonl", "[cli][resolve_output_format]") {
    REQUIRE(resolve_output_format(OutputFormat::Jsonl) == OutputFormat::Jsonl);
}

TEST_CASE("parse_format_arg: valid values", "[cli][parse_format_arg]") {
    REQUIRE(parse_format_arg("pretty") == OutputFormat::Pretty);
    REQUIRE(parse_format_arg("tsv") == OutputFormat::Tsv);
    REQUIRE(parse_format_arg("jsonl") == OutputFormat::Jsonl);
}

TEST_CASE("parse_format_arg: invalid value throws", "[cli][parse_format_arg]") {
    REQUIRE_THROWS_AS(parse_format_arg("invalid"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_format_arg("json"), std::runtime_error);  // Not jsonl
}

TEST_CASE("parse_format_arg: case insensitive", "[cli][parse_format_arg]") {
    // Tolerant of the user typing PRETTY / Tsv / JsonL.
    REQUIRE(parse_format_arg("PRETTY") == OutputFormat::Pretty);
    REQUIRE(parse_format_arg("Tsv") == OutputFormat::Tsv);
    REQUIRE(parse_format_arg("JsonL") == OutputFormat::Jsonl);
}

// ============================================================================
// Provenance CLI option tests
// ============================================================================

TEST_CASE("parse_start_arguments: --provenance-type sets config", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start", "--provenance-type", "python-client"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type == "python-client");
}

TEST_CASE("parse_start_arguments: --provenance-uuid sets config", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start", "--provenance-uuid", "550e8400-e29b-41d4-a716-446655440000"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_uuid == "550e8400-e29b-41d4-a716-446655440000");
}

TEST_CASE("parse_start_arguments: --provenance-version sets config", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start", "--provenance-version", "1.2.3"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_version == "1.2.3");
}

TEST_CASE("parse_start_arguments: all provenance options combined", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start",
        "--provenance-type", "macos-gui",
        "--provenance-uuid", "12345678-1234-1234-1234-123456789abc",
        "--provenance-version", "2.0.0"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type == "macos-gui");
    REQUIRE(config.provenance_uuid == "12345678-1234-1234-1234-123456789abc");
    REQUIRE(config.provenance_version == "2.0.0");
}

TEST_CASE("parse_start_arguments: provenance type values", "[cli][parse_start_arguments][provenance]") {
    SECTION("terminal type") {
        ArgvHelper args{"beebium", "start", "--provenance-type", "terminal"};
        ServerConfig<MachineType> config;
        parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
        REQUIRE(config.provenance_type == "terminal");
    }

    SECTION("unknown type") {
        ArgvHelper args{"beebium", "start", "--provenance-type", "unknown"};
        ServerConfig<MachineType> config;
        parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
        REQUIRE(config.provenance_type == "unknown");
    }

    SECTION("typescript-oracle type") {
        ArgvHelper args{"beebium", "start", "--provenance-type", "typescript-oracle"};
        ServerConfig<MachineType> config;
        parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
        REQUIRE(config.provenance_type == "typescript-oracle");
    }
}

TEST_CASE("parse_start_arguments: provenance defaults are empty", "[cli][parse_start_arguments][provenance]") {
    ArgvHelper args{"beebium", "start"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type.empty());
    REQUIRE(config.provenance_uuid.empty());
    REQUIRE(config.provenance_version.empty());
}

// ============================================================================
// UUID validation function tests
// ============================================================================

TEST_CASE("is_valid_uuid: valid UUIDs", "[cli][uuid]") {
    REQUIRE(is_valid_uuid("550e8400-e29b-41d4-a716-446655440000"));
    REQUIRE(is_valid_uuid("12345678-1234-1234-1234-123456789abc"));
    REQUIRE(is_valid_uuid("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF"));
    REQUIRE(is_valid_uuid("00000000-0000-0000-0000-000000000000"));
    REQUIRE(is_valid_uuid("abcdef01-2345-6789-abcd-ef0123456789"));
}

TEST_CASE("is_valid_uuid: invalid UUIDs", "[cli][uuid]") {
    REQUIRE_FALSE(is_valid_uuid(""));
    REQUIRE_FALSE(is_valid_uuid("not-a-uuid"));
    REQUIRE_FALSE(is_valid_uuid("550e8400e29b41d4a716446655440000"));  // No hyphens
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-44665544000"));   // Too short
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-4466554400000")); // Too long
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a716-44665544000g"));  // Invalid hex char
    REQUIRE_FALSE(is_valid_uuid("550e8400-e29b-41d4-a71-6446655440000"));  // Wrong position hyphen
}

TEST_CASE("generate_uuid_v4: generates valid format", "[cli][uuid]") {
    auto uuid = generate_uuid_v4();

    REQUIRE(uuid.length() == 36);
    REQUIRE(is_valid_uuid(uuid));

    // Check version 4 indicator (character at position 14 should be '4')
    REQUIRE(uuid[14] == '4');

    // Check variant (character at position 19 should be 8, 9, a, or b)
    char variant = uuid[19];
    REQUIRE((variant == '8' || variant == '9' || variant == 'a' || variant == 'b'));
}

TEST_CASE("generate_uuid_v4: generates unique values", "[cli][uuid]") {
    auto uuid1 = generate_uuid_v4();
    auto uuid2 = generate_uuid_v4();
    auto uuid3 = generate_uuid_v4();

    REQUIRE(uuid1 != uuid2);
    REQUIRE(uuid2 != uuid3);
    REQUIRE(uuid1 != uuid3);
}

// ============================================================================
// Machine Identity CLI option tests
// ============================================================================

TEST_CASE("parse_start_arguments: --machine-uuid sets config", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-uuid", "550e8400-e29b-41d4-a716-446655440000"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_uuid == "550e8400-e29b-41d4-a716-446655440000");
}

TEST_CASE("parse_start_arguments: --machine-uuid rejects invalid UUID", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-uuid", "not-a-uuid"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --machine-name sets config", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-name", "My BBC Micro"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_name == "My BBC Micro");
}

TEST_CASE("parse_start_arguments: --machine-name allows spaces", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start", "--machine-name", "Level 3 Fileserver"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_name == "Level 3 Fileserver");
}

TEST_CASE("parse_start_arguments: all identity options combined", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start",
        "--machine-uuid", "12345678-1234-1234-1234-123456789abc",
        "--machine-name", "Teletext Server"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_uuid == "12345678-1234-1234-1234-123456789abc");
    REQUIRE(config.machine_name == "Teletext Server");
}

TEST_CASE("parse_start_arguments: identity defaults are empty", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.machine_uuid.empty());
    REQUIRE(config.machine_name.empty());
}

TEST_CASE("parse_start_arguments: identity and provenance combined", "[cli][parse_start_arguments][identity]") {
    ArgvHelper args{"beebium", "start",
        "--provenance-type", "python-client",
        "--provenance-uuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
        "--machine-uuid", "11111111-2222-3333-4444-555555555555",
        "--machine-name", "Test Server"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.provenance_type == "python-client");
    REQUIRE(config.provenance_uuid == "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    REQUIRE(config.machine_uuid == "11111111-2222-3333-4444-555555555555");
    REQUIRE(config.machine_name == "Test Server");
}

// ============================================================================
// describe-preset-schema subcommand tests
// ============================================================================

TEST_CASE("describe-preset-schema: subcommand is registered", "[cli][describe-preset-schema]") {
    const auto& cmds = get_subcommands<MachineType>();

    bool found = false;
    for (const auto& cmd : cmds) {
        if (cmd->name() == "describe-preset-schema") {
            found = true;
            break;
        }
    }

    REQUIRE(found);
}

TEST_CASE("describe-preset-schema: has non-empty description", "[cli][describe-preset-schema]") {
    const auto& cmds = get_subcommands<MachineType>();

    for (const auto& cmd : cmds) {
        if (cmd->name() == "describe-preset-schema") {
            REQUIRE_FALSE(cmd->description().empty());
            return;
        }
    }
    FAIL("describe-preset-schema subcommand not found");
}

TEST_CASE("describe-preset-schema: --help returns OK", "[cli][describe-preset-schema]") {
    ArgvHelper args{"beebium", "describe-preset-schema", "--help"};
    GlobalConfig global;
    global.subcommand_name = "describe-preset-schema";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("describe-preset-schema: returns OK with no arguments", "[cli][describe-preset-schema]") {
    ArgvHelper args{"beebium", "describe-preset-schema"};
    GlobalConfig global;
    global.subcommand_name = "describe-preset-schema";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("describe-preset-schema: unknown argument returns USAGE", "[cli][describe-preset-schema]") {
    ArgvHelper args{"beebium", "describe-preset-schema", "--unknown"};
    GlobalConfig global;
    global.subcommand_name = "describe-preset-schema";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

// ============================================================================
// --preset option tests
// ============================================================================

#ifndef BEEBIUM_TEST_ASSETS_DIR
#error "BEEBIUM_TEST_ASSETS_DIR must be defined"
#endif

namespace {
    std::filesystem::path presets_dirpath() {
        return std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) / "presets";
    }
}

TEST_CASE("parse_start_arguments: --preset sets preset_filepath", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "minimal.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.preset_filepath.has_value());
    REQUIRE(config.preset_filepath->filename() == "minimal.preset.beebium");
}

TEST_CASE("parse_start_arguments: --preset applies fdc_socket_id", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "acorn-1770");
}

TEST_CASE("parse_start_arguments: --preset applies floppy_drives", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0] == "file:///tmp/test.ssd");
    REQUIRE(config.floppy_filepaths[1].empty());  // Explicit null in preset
}

TEST_CASE("parse_start_arguments: CLI --fdc overrides preset fdc", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str(), "--fdc", "none"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "none");  // CLI overrides preset's "acorn-1770"
}

TEST_CASE("parse_start_arguments: CLI --floppy overrides preset floppy", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str(),
                    "--floppy", "0:file:///other/disc.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0] == "file:///other/disc.ssd");  // CLI overrides preset
}

TEST_CASE("parse_start_arguments: --preset applies sideways_bank slots", "[cli][parse_start_arguments][preset][sideways]") {
    auto preset_path = presets_dirpath() / "sideways.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    // Preset slot 14 ROM and slot 13 empty are folded into the active config
    // and routed for loading (proves the merge is wired into parse).
    REQUIRE(config.rom_slots[14] == "acorn-dfs_2_26.rom");
    REQUIRE(config.rom_slots[13] == EMPTY_SLOT_MARKER);
    int slot14_rom = 0;
    for (const auto& c : config.sideways_configs) {
        if (c.slot == 14 && c.type == SidewaysSlotType::Rom) ++slot14_rom;
    }
    REQUIRE(slot14_rom == 1);
}

TEST_CASE("parse_start_arguments: CLI --sideways overrides preset sideways slot", "[cli][parse_start_arguments][preset][sideways]") {
    auto preset_path = presets_dirpath() / "sideways.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str(),
                    "--sideways", "slot=14:type=empty"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    // CLI wins for slot 14: empty, not the preset's DFS ROM, and no duplicate.
    REQUIRE(config.rom_slots[14] == EMPTY_SLOT_MARKER);
    int slot14_count = 0;
    for (const auto& c : config.sideways_configs) {
        if (c.slot == 14) ++slot14_count;
    }
    REQUIRE(slot14_count == 1);
}

TEST_CASE("parse_start_arguments: --preset before other options", "[cli][parse_start_arguments][preset]") {
    // Preset comes first, then CLI options override
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str(),
                    "--fdc", "none", "--floppy", "1:file:///drive1.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "none");  // CLI override
    REQUIRE(config.floppy_filepaths[0] == "file:///tmp/test.ssd");  // From preset
    REQUIRE(config.floppy_filepaths[1] == "file:///drive1.ssd");  // CLI override
}

TEST_CASE("parse_start_arguments: --preset after other options", "[cli][parse_start_arguments][preset]") {
    // CLI options before --preset still override (first pass finds preset regardless of position)
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--fdc", "none", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "none");  // CLI overrides preset
}

TEST_CASE("parse_start_arguments: --preset with nonexistent file returns error", "[cli][parse_start_arguments][preset]") {
    ArgvHelper args{"beebium", "start", "--preset", "/nonexistent/path.preset.beebium"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::NOINPUT);
}

TEST_CASE("parse_start_arguments: --preset with invalid JSON returns error", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "invalid.json";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::NOINPUT);
}

TEST_CASE("parse_start_arguments: preset without storage section leaves defaults", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "minimal.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type.empty());  // Not set by preset
    REQUIRE(config.floppy_filepaths[0].empty());
    REQUIRE(config.floppy_filepaths[1].empty());
}

TEST_CASE("parse_start_arguments: --preset with wrong model returns CONFIG error", "[cli][parse_start_arguments][preset]") {
    auto preset_path = presets_dirpath() / "wrong_model.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::CONFIG);
}

TEST_CASE("parse_start_arguments: --preset with matching model succeeds", "[cli][parse_start_arguments][preset]") {
    // The storage.preset.beebium has model: "model-b" which matches MachineType (ModelB)
    auto preset_path = presets_dirpath() / "storage.preset.beebium";
    ArgvHelper args{"beebium", "start", "--preset", preset_path.string().c_str()};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "acorn-1770");
}

// ============================================================================
// describe-preset-schema JSON output tests
// ============================================================================

// Helper to capture stdout from a subcommand invocation
namespace {
    template<typename MachineType>
    std::string capture_describe_preset_schema_output() {
        ArgvHelper args{"beebium", "describe-preset-schema"};
        GlobalConfig global;
        global.subcommand_name = "describe-preset-schema";
        global.subcommand_argv_start = 2;

        // Capture stdout
        std::stringstream buffer;
        std::streambuf* old_cout = std::cout.rdbuf(buffer.rdbuf());

        dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

        // Restore stdout
        std::cout.rdbuf(old_cout);
        return buffer.str();
    }
}

TEST_CASE("describe-preset-schema: outputs valid JSON", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();

    // Parse as JSON - this will throw if invalid
    nlohmann::json json;
    REQUIRE_NOTHROW(json = nlohmann::json::parse(output));
    REQUIRE(json.is_object());
}

TEST_CASE("describe-preset-schema: has schema_version", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    REQUIRE(json.contains("schema_version"));
    REQUIRE(json["schema_version"].is_number_integer());
    REQUIRE(json["schema_version"].get<int>() == 1);
}

TEST_CASE("describe-preset-schema: has model info", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    REQUIRE(json.contains("model"));
    REQUIRE(json["model"].is_object());
    REQUIRE(json["model"].contains("id"));
    REQUIRE(json["model"].contains("name"));
    REQUIRE(json["model"].contains("description"));

    // Model B specific values
    REQUIRE(json["model"]["id"].get<std::string>() == "model-b");
    REQUIRE(json["model"]["name"].get<std::string>() == "BBC Model B");
    REQUIRE_FALSE(json["model"]["description"].get<std::string>().empty());
}

TEST_CASE("describe-preset-schema: has sections array", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    REQUIRE(json.contains("sections"));
    REQUIRE(json["sections"].is_array());
    REQUIRE(json["sections"].size() >= 1);
}

TEST_CASE("describe-preset-schema: Model B has storage section with fdc_socket", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }
    REQUIRE_FALSE(storage.is_null());
    REQUIRE(storage["type"] == "storage");

    // Model B has fdc_socket (not built-in)
    REQUIRE(storage.contains("fdc_socket"));
    REQUIRE(storage["fdc_socket"].is_object());
    REQUIRE(storage["fdc_socket"].contains("options"));
    REQUIRE(storage["fdc_socket"]["options"].is_array());

    // Built-in FDC should be null for Model B
    REQUIRE(storage["builtin"]["fdc"].is_null());
}

TEST_CASE("describe-preset-schema: Model B fdc_socket has none option", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    // Look for "none" option
    bool found_none = false;
    for (const auto& option : storage["fdc_socket"]["options"]) {
        if (option["id"] == "none") {
            found_none = true;
            REQUIRE(option["label"] == "Empty");
            REQUIRE(option["device"].is_null());
            break;
        }
    }
    REQUIRE(found_none);
}

TEST_CASE("describe-preset-schema: Model B fdc_socket has acorn-1770 option", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    // Look for "acorn-1770" option
    bool found_acorn_1770 = false;
    for (const auto& option : storage["fdc_socket"]["options"]) {
        if (option["id"] == "acorn-1770") {
            found_acorn_1770 = true;
            REQUIRE(option["label"] == "Acorn 1770");
            REQUIRE(option["device"] == "WD1770");
            REQUIRE_FALSE(option["description"].get<std::string>().empty());
            break;
        }
    }
    REQUIRE(found_acorn_1770);
}

TEST_CASE("describe-preset-schema: storage section has floppy_drives", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    REQUIRE(storage.contains("floppy_drives"));
    REQUIRE(storage["floppy_drives"].is_array());
    REQUIRE(storage["floppy_drives"].size() == 2);  // Drives 0 and 1

    // Check drive 0
    auto& drive0 = storage["floppy_drives"][0];
    REQUIRE(drive0["drive"].get<int>() == 0);
    REQUIRE(drive0["tracks"].is_array());
    REQUIRE(drive0["sides"].get<int>() == 2);
    REQUIRE(drive0["image_types"].is_array());

    // Check drive 1
    auto& drive1 = storage["floppy_drives"][1];
    REQUIRE(drive1["drive"].get<int>() == 1);
}

TEST_CASE("describe-preset-schema: floppy_drives has correct tracks", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    auto& drive0 = storage["floppy_drives"][0];
    auto& tracks = drive0["tracks"];
    REQUIRE(tracks.size() == 2);

    // Should contain 40 and 80
    std::vector<int> track_values = tracks.get<std::vector<int>>();
    REQUIRE(std::find(track_values.begin(), track_values.end(), 40) != track_values.end());
    REQUIRE(std::find(track_values.begin(), track_values.end(), 80) != track_values.end());
}

TEST_CASE("describe-preset-schema: floppy_drives has correct image_types", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    auto& drive0 = storage["floppy_drives"][0];
    auto& image_types = drive0["image_types"];
    std::vector<std::string> types = image_types.get<std::vector<std::string>>();

    REQUIRE(std::find(types.begin(), types.end(), "ssd") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "dsd") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "adf") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "adl") != types.end());
}

TEST_CASE("describe-preset-schema: builtin has cassette true", "[cli][describe-preset-schema][json]") {
    auto output = capture_describe_preset_schema_output<MachineType>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    REQUIRE(storage.contains("builtin"));
    REQUIRE(storage["builtin"]["cassette"].get<bool>() == true);
}

// Test Model B+ output (different machine type with built-in FDC)
using ModelBPlus = beebium::ModelBPlus;

TEST_CASE("describe-preset-schema: Model B+ has built-in FDC, no fdc_socket", "[cli][describe-preset-schema][json][model-b-plus]") {
    auto output = capture_describe_preset_schema_output<ModelBPlus>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }
    REQUIRE_FALSE(storage.is_null());

    // Model B+ has built-in FDC
    REQUIRE(storage["builtin"]["fdc"].is_object());
    REQUIRE(storage["builtin"]["fdc"]["device"] == "WD1770");
    REQUIRE(storage["builtin"]["fdc"]["label"] == "Built-in WD1770");

    // Model B+ should NOT have fdc_socket key
    REQUIRE_FALSE(storage.contains("fdc_socket"));
}

TEST_CASE("describe-preset-schema: Model B+ has floppy_drives", "[cli][describe-preset-schema][json][model-b-plus]") {
    auto output = capture_describe_preset_schema_output<ModelBPlus>();
    auto json = nlohmann::json::parse(output);

    // Find storage section
    nlohmann::json storage;
    for (const auto& section : json["sections"]) {
        if (section.contains("type") && section["type"] == "storage") {
            storage = section;
            break;
        }
    }

    REQUIRE(storage.contains("floppy_drives"));
    REQUIRE(storage["floppy_drives"].is_array());
    REQUIRE(storage["floppy_drives"].size() == 2);
}

TEST_CASE("describe-preset-schema: Model B+ model info is correct", "[cli][describe-preset-schema][json][model-b-plus]") {
    auto output = capture_describe_preset_schema_output<ModelBPlus>();
    auto json = nlohmann::json::parse(output);

    REQUIRE(json["model"]["id"].get<std::string>() == "model-b-plus");
    REQUIRE(json["model"]["name"].get<std::string>() == "BBC Model B+ 64K");
}

// ============================================================================
// list-presets subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: list-presets subcommand", "[cli][parse_global_arguments][list-presets]") {
    ArgvHelper args{"beebium", "list-presets"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "list-presets");
}

TEST_CASE("dispatch_subcommand: list-presets returns OK", "[cli][dispatch_subcommand][list-presets]") {
    ArgvHelper args{"beebium", "list-presets"};
    GlobalConfig global;
    global.subcommand_name = "list-presets";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: list-presets --json returns OK", "[cli][dispatch_subcommand][list-presets]") {
    ArgvHelper args{"beebium", "list-presets", "--json"};
    GlobalConfig global;
    global.subcommand_name = "list-presets";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: list-presets --help returns OK", "[cli][dispatch_subcommand][list-presets]") {
    ArgvHelper args{"beebium", "list-presets", "--help"};
    GlobalConfig global;
    global.subcommand_name = "list-presets";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: list-presets with unknown arg returns USAGE", "[cli][dispatch_subcommand][list-presets]") {
    ArgvHelper args{"beebium", "list-presets", "--unknown"};
    GlobalConfig global;
    global.subcommand_name = "list-presets";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

// ============================================================================
// show-preset subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: show-preset subcommand", "[cli][parse_global_arguments][show-preset]") {
    ArgvHelper args{"beebium", "show-preset", "test-id"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "show-preset");
}

TEST_CASE("dispatch_subcommand: show-preset without ID returns USAGE", "[cli][dispatch_subcommand][show-preset]") {
    ArgvHelper args{"beebium", "show-preset"};
    GlobalConfig global;
    global.subcommand_name = "show-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: show-preset with nonexistent ID returns NOINPUT", "[cli][dispatch_subcommand][show-preset]") {
    ArgvHelper args{"beebium", "show-preset", "nonexistent-preset-12345"};
    GlobalConfig global;
    global.subcommand_name = "show-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::NOINPUT);
}

TEST_CASE("dispatch_subcommand: show-preset --help returns OK", "[cli][dispatch_subcommand][show-preset]") {
    ArgvHelper args{"beebium", "show-preset", "--help"};
    GlobalConfig global;
    global.subcommand_name = "show-preset";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

// ============================================================================
// report-presets-dirpath subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: report-presets-dirpath subcommand", "[cli][parse_global_arguments][report-presets-dirpath]") {
    ArgvHelper args{"beebium", "report-presets-dirpath"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "report-presets-dirpath");
}

TEST_CASE("dispatch_subcommand: report-presets-dirpath returns OK", "[cli][dispatch_subcommand][report-presets-dirpath]") {
    ArgvHelper args{"beebium", "report-presets-dirpath"};
    GlobalConfig global;
    global.subcommand_name = "report-presets-dirpath";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: report-presets-dirpath --help returns OK", "[cli][dispatch_subcommand][report-presets-dirpath]") {
    ArgvHelper args{"beebium", "report-presets-dirpath", "--help"};
    GlobalConfig global;
    global.subcommand_name = "report-presets-dirpath";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: report-presets-dirpath with unknown arg returns USAGE", "[cli][dispatch_subcommand][report-presets-dirpath]") {
    ArgvHelper args{"beebium", "report-presets-dirpath", "--unknown"};
    GlobalConfig global;
    global.subcommand_name = "report-presets-dirpath";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

// ============================================================================
// create-preset subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: create-preset subcommand", "[cli][parse_global_arguments][create-preset]") {
    ArgvHelper args{"beebium", "create-preset", "--name", "Test"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "create-preset");
}

TEST_CASE("dispatch_subcommand: create-preset without --name returns USAGE", "[cli][dispatch_subcommand][create-preset]") {
    ArgvHelper args{"beebium", "create-preset"};
    GlobalConfig global;
    global.subcommand_name = "create-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: create-preset --help returns OK", "[cli][dispatch_subcommand][create-preset]") {
    ArgvHelper args{"beebium", "create-preset", "--help"};
    GlobalConfig global;
    global.subcommand_name = "create-preset";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: create-preset --from nonexistent returns NOINPUT", "[cli][dispatch_subcommand][create-preset]") {
    ArgvHelper args{"beebium", "create-preset", "--name", "Test", "--from", "nonexistent-12345"};
    GlobalConfig global;
    global.subcommand_name = "create-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::NOINPUT);
}

// ============================================================================
// delete-preset subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: delete-preset subcommand", "[cli][parse_global_arguments][delete-preset]") {
    ArgvHelper args{"beebium", "delete-preset", "test-id"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "delete-preset");
}

TEST_CASE("dispatch_subcommand: delete-preset without ID returns USAGE", "[cli][dispatch_subcommand][delete-preset]") {
    ArgvHelper args{"beebium", "delete-preset"};
    GlobalConfig global;
    global.subcommand_name = "delete-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: delete-preset with nonexistent ID returns NOINPUT", "[cli][dispatch_subcommand][delete-preset]") {
    ArgvHelper args{"beebium", "delete-preset", "nonexistent-preset-12345"};
    GlobalConfig global;
    global.subcommand_name = "delete-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::NOINPUT);
}

TEST_CASE("dispatch_subcommand: delete-preset --help returns OK", "[cli][dispatch_subcommand][delete-preset]") {
    ArgvHelper args{"beebium", "delete-preset", "--help"};
    GlobalConfig global;
    global.subcommand_name = "delete-preset";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

// ============================================================================
// import-preset subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: import-preset subcommand", "[cli][parse_global_arguments][import-preset]") {
    ArgvHelper args{"beebium", "import-preset", "/path/to/preset.preset.beebium"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "import-preset");
}

TEST_CASE("dispatch_subcommand: import-preset without filepath returns USAGE", "[cli][dispatch_subcommand][import-preset]") {
    ArgvHelper args{"beebium", "import-preset"};
    GlobalConfig global;
    global.subcommand_name = "import-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: import-preset with nonexistent file returns NOINPUT", "[cli][dispatch_subcommand][import-preset]") {
    ArgvHelper args{"beebium", "import-preset", "/nonexistent/path/to/preset.preset.beebium"};
    GlobalConfig global;
    global.subcommand_name = "import-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::NOINPUT);
}

TEST_CASE("dispatch_subcommand: import-preset --help returns OK", "[cli][dispatch_subcommand][import-preset]") {
    ArgvHelper args{"beebium", "import-preset", "--help"};
    GlobalConfig global;
    global.subcommand_name = "import-preset";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

TEST_CASE("dispatch_subcommand: import-preset with invalid JSON returns DATAERR", "[cli][dispatch_subcommand][import-preset]") {
    // Create a temporary file with invalid JSON
    auto temp_filepath = std::filesystem::temp_directory_path() / "invalid-test.preset.beebium";
    {
        std::ofstream f(temp_filepath);
        f << "{ not valid json }";
    }

    ArgvHelper args{"beebium", "import-preset", temp_filepath.string().c_str()};
    GlobalConfig global;
    global.subcommand_name = "import-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    std::filesystem::remove(temp_filepath);

    REQUIRE(result == ExitCode::DATAERR);
}

TEST_CASE("dispatch_subcommand: import-preset with wrong model returns DATAERR", "[cli][dispatch_subcommand][import-preset]") {
    // Create a temporary file with a different model
    auto temp_filepath = std::filesystem::temp_directory_path() / "wrong-model-test.preset.beebium";
    {
        std::ofstream f(temp_filepath);
        f << R"({"model": "wrong-model-12345", "name": "Wrong Model Test"})";
    }

    ArgvHelper args{"beebium", "import-preset", temp_filepath.string().c_str()};
    GlobalConfig global;
    global.subcommand_name = "import-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    std::filesystem::remove(temp_filepath);

    REQUIRE(result == ExitCode::DATAERR);
}

// ============================================================================
// export-preset subcommand tests
// ============================================================================

TEST_CASE("parse_global_arguments: export-preset subcommand", "[cli][parse_global_arguments][export-preset]") {
    ArgvHelper args{"beebium", "export-preset", "test-id", "--output", "/path/to/output.preset.beebium"};
    GlobalConfig global;

    auto result = parse_global_arguments(args.argc(), args.data(), global);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(global.subcommand_name == "export-preset");
}

TEST_CASE("dispatch_subcommand: export-preset without ID returns USAGE", "[cli][dispatch_subcommand][export-preset]") {
    ArgvHelper args{"beebium", "export-preset", "--output", "/tmp/out.preset.beebium"};
    GlobalConfig global;
    global.subcommand_name = "export-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: export-preset without --output returns USAGE", "[cli][dispatch_subcommand][export-preset]") {
    ArgvHelper args{"beebium", "export-preset", "test-id"};
    GlobalConfig global;
    global.subcommand_name = "export-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::USAGE);
}

TEST_CASE("dispatch_subcommand: export-preset with nonexistent preset returns NOINPUT", "[cli][dispatch_subcommand][export-preset]") {
    ArgvHelper args{"beebium", "export-preset", "nonexistent-preset-12345", "--output", "/tmp/out.preset.beebium"};
    GlobalConfig global;
    global.subcommand_name = "export-preset";
    global.subcommand_argv_start = 2;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::NOINPUT);
}

TEST_CASE("dispatch_subcommand: export-preset --help returns OK", "[cli][dispatch_subcommand][export-preset]") {
    ArgvHelper args{"beebium", "export-preset", "--help"};
    GlobalConfig global;
    global.subcommand_name = "export-preset";
    global.subcommand_argv_start = 2;
    global.help_requested = true;

    auto result = dispatch_subcommand<MachineType>(args.argc(), args.data(), global);

    REQUIRE(result == ExitCode::OK);
}

// ============================================================================
// parse_start_arguments() — Econet options
// ============================================================================

TEST_CASE("parse_start_arguments: --station sets station_number", "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--station", "1"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.station_number == 1);
}

TEST_CASE("parse_start_arguments: --station 254 is valid", "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--station", "254"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.station_number == 254);
}

TEST_CASE("parse_start_arguments: --station 0 returns USAGE", "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--station", "0"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --station 255 returns USAGE", "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--station", "255"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --station non-numeric returns USAGE", "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--station", "abc"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: default station_number is -1", "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.station_number == -1);
}

// AUN is now selected via the generic extension dispatch:
//
//   --aun port=42001:map=0.254@127.0.0.1@32768
//
// parse_start_arguments turns that into one ExtensionInstance with
// name="aun" and a config map. The actual binding/peer-table setup is
// delegated to AunEconetTransportExtension::create_backend, exercised
// in test_aun_econet_transport_extension.cpp.

namespace {
const typename ServerConfig<MachineType>::ExtensionInstance*
find_extension_instance(const ServerConfig<MachineType>& config,
                        std::string_view name) {
    for (const auto& inst : config.extension_instances) {
        if (inst.name == name) return &inst;
    }
    return nullptr;
}
}

TEST_CASE("parse_start_arguments: --aun port=42001 produces aun extension instance",
          "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--aun", "port=42001"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    auto* inst = find_extension_instance(config, "aun");
    REQUIRE(inst != nullptr);
    REQUIRE(inst->config.at("port") == "42001");
}

TEST_CASE("parse_start_arguments: --aun port=none stores 'none' in extension config",
          "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--aun", "port=none"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    auto* inst = find_extension_instance(config, "aun");
    REQUIRE(inst != nullptr);
    REQUIRE(inst->config.at("port") == "none");
}

TEST_CASE("parse_start_arguments: --aun map= is repeatable and accumulates",
          "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start", "--aun",
                    "port=42001:map=0.254@192.168.1.10@32768:map=0.1@192.168.1.20@42002"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    auto* inst = find_extension_instance(config, "aun");
    REQUIRE(inst != nullptr);
    REQUIRE(inst->config.at("port") == "42001");
    // is_list params land in list_config as a vector of opaque tokens.
    REQUIRE(inst->config.count("map") == 0);
    REQUIRE(inst->list_config.at("map") == std::vector<std::string>{
        "0.254@192.168.1.10@32768", "0.1@192.168.1.20@42002"});
}

TEST_CASE("parse_start_arguments: default has no aun extension instance",
          "[cli][parse_start_arguments][econet]") {
    // With the cutover, just specifying --station no longer auto-enables
    // AUN. The user must explicitly say --aun to bind a UDP socket.
    ArgvHelper args{"beebium", "start"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(find_extension_instance(config, "aun") == nullptr);
}

TEST_CASE("parse_start_arguments: --aun and --station combined",
          "[cli][parse_start_arguments][econet]") {
    ArgvHelper args{"beebium", "start",
                    "--station", "1",
                    "--aun", "port=42001:map=0.254@127.0.0.1@32768"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.station_number == 1);
    auto* inst = find_extension_instance(config, "aun");
    REQUIRE(inst != nullptr);
    REQUIRE(inst->config.at("port") == "42001");
    REQUIRE(inst->config.count("map") == 0);
    REQUIRE(inst->list_config.at("map") == std::vector<std::string>{
        "0.254@127.0.0.1@32768"});
}

// ============================================================================
// parse_start_arguments() — options migrated from test_server_main.cpp
// ============================================================================

TEST_CASE("parse_start_arguments: --rom-dir sets rom_dirpath", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--rom-dir", "/path/to/roms"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.rom_dirpath == "/path/to/roms");
}

TEST_CASE("parse_start_arguments: --sideways rom populates sideways_configs and rom_slots", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--sideways", "slot=15:type=rom:image=test.rom"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 15);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Rom);
    REQUIRE(config.sideways_configs[0].image_filepath == "test.rom");
    REQUIRE(config.rom_slots.count(15) == 1);
    REQUIRE(config.rom_slots[15] == "test.rom");
}

TEST_CASE("parse_start_arguments: --sideways empty populates config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--sideways", "slot=11:type=empty"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 11);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Empty);
}

TEST_CASE("parse_start_arguments: --sideways ram populates config", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--sideways", "slot=4:type=ram"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 4);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Ram);
    REQUIRE_FALSE(config.sideways_configs[0].write_protected);
}

TEST_CASE("parse_start_arguments: --floppy sets floppy_filepaths", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--floppy", "0:game.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0] == "game.ssd");
    REQUIRE(config.floppy_filepaths[1].empty());
}

TEST_CASE("parse_start_arguments: --floppy for drive 1", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--floppy", "1:backup.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0].empty());
    REQUIRE(config.floppy_filepaths[1] == "backup.ssd");
}

TEST_CASE("parse_start_arguments: --floppy with split filepath (colon completion)", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--floppy", "0:", "game.ssd"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.floppy_filepaths[0] == "game.ssd");
}

TEST_CASE("parse_start_arguments: --sideways ram with write-protect flag", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--sideways", "slot=15:type=ram:write-protect"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].slot == 15);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Ram);
    REQUIRE(config.sideways_configs[0].write_protected);
}

TEST_CASE("parse_start_arguments: legacy positional --sideways is rejected", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--sideways", "15:ram"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE(result.has_value());  // parse error -> non-empty exit code
}

TEST_CASE("parse_start_arguments: --floppy colon completion doesn't consume options", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--floppy", "0:", "--port", "8080"};
    ServerConfig<MachineType> config;

    // Returns ExitCode::USAGE rather than throwing -- a CLI typo must
    // produce a clean error, not abort the process (#33).
    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);
    REQUIRE(result.has_value());
    REQUIRE(*result == ExitCode::USAGE);
}

TEST_CASE("parse_start_arguments: --sideways does not consume a following option", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--sideways", "slot=15:type=empty", "--port", "8080"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.sideways_configs.size() == 1);
    REQUIRE(config.sideways_configs[0].type == SidewaysSlotType::Empty);
    REQUIRE(config.port == 8080);
}

TEST_CASE("parse_start_arguments: --auto-boot sets auto_boot", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--auto-boot"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.auto_boot == true);
    REQUIRE(config.auto_boot_set == true);
}

TEST_CASE("parse_start_arguments: --wait=cli sets wait_mode", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--wait=cli"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.wait_mode == WaitMode::Cli);
}

TEST_CASE("parse_start_arguments: --wait=api sets wait_mode", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--wait=api"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.wait_mode == WaitMode::Api);
}

TEST_CASE("parse_start_arguments: --fdc sets fdc_type", "[cli][parse_start_arguments]") {
    ArgvHelper args{"beebium", "start", "--fdc", "acorn-1770"};
    ServerConfig<MachineType> config;

    auto result = parse_start_arguments<MachineType>(args.argc(), args.data(), 2, config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(config.fdc_type == "acorn-1770");
}

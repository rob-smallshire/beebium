// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Integration tests for `list-extensions` and `describe-extension` subcommands.
//
// These tests run the actual server executable as a subprocess to verify
// end-to-end that the auto-default extension directory is scanned, that
// a user-supplied --extension-dir is honoured, and that the rendered
// output matches the requested --format.

#include <catch2/catch_test_macros.hpp>
#include <beebium/PlatformUtils.hpp>
#include <nlohmann/json.hpp>
#include "test_temp_paths.hpp"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

struct ProcessResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

ProcessResult run_command(const std::string& command) {
    auto stdout_filepath = beebium::test::unique_temp_path("beebium_ext_test_stdout_", ".txt");
    auto stderr_filepath = beebium::test::unique_temp_path("beebium_ext_test_stderr_", ".txt");

    std::string full_command = command;
#ifdef _WIN32
    full_command += " >\"" + stdout_filepath.string() + "\" 2>\"" + stderr_filepath.string() + "\"";
#else
    full_command += " >" + stdout_filepath.string() + " 2>" + stderr_filepath.string();
#endif

    ProcessResult result;
    result.exit_code = std::system(full_command.c_str());
#ifndef _WIN32
    if (WIFEXITED(result.exit_code)) {
        result.exit_code = WEXITSTATUS(result.exit_code);
    }
#endif

    if (std::ifstream f(stdout_filepath); f) {
        std::ostringstream ss; ss << f.rdbuf(); result.stdout_output = ss.str();
    }
    if (std::ifstream f(stderr_filepath); f) {
        std::ostringstream ss; ss << f.rdbuf(); result.stderr_output = ss.str();
    }

    std::filesystem::remove(stdout_filepath);
    std::filesystem::remove(stderr_filepath);

    return result;
}

std::filesystem::path find_executable(const std::string& name) {
#ifdef _WIN32
    std::string exe_name = name + ".exe";
#else
    std::string exe_name = name;
#endif
    if (auto env = beebium::platform::get_env("BEEBIUM_SERVERS_DIRPATH")) {
        auto p = std::filesystem::path(*env) / exe_name;
        if (std::filesystem::exists(p)) return p;
    }
    std::vector<std::filesystem::path> search_paths = {
        std::filesystem::current_path() / "src" / "server" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / exe_name,
        std::filesystem::current_path() / "build" / "src" / "server" / exe_name,
#ifdef _WIN32
        // MSBuild puts executables under a per-config subdirectory.
        std::filesystem::current_path() / "src" / "server" / "Release" / exe_name,
        std::filesystem::current_path() / "src" / "server" / "Debug" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / "Release" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / "Debug" / exe_name,
#endif
    };
    for (const auto& p : search_paths) {
        if (std::filesystem::exists(p)) return p;
    }
    return exe_name;
}

const std::string EXECUTABLE = find_executable("beebium-model-b-romram").string();

}  // namespace

TEST_CASE("list-extensions exits successfully with no args",
          "[integration][extension][list-extensions]") {
    auto r = run_command(EXECUTABLE + " list-extensions");
    INFO("stdout: " << r.stdout_output);
    INFO("stderr: " << r.stderr_output);
    REQUIRE(r.exit_code == 0);
}

TEST_CASE("list-extensions includes the tube-65c02 coprocessor plugin",
          "[integration][extension][list-extensions]") {
    // tube-65c02 is a plugin now, loaded from the default extensions directory
    // beside the executable, like acorn-rtc.
    auto r = run_command(EXECUTABLE + " list-extensions");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    REQUIRE(r.stdout_output.find("tube-65c02") != std::string::npos);
}

TEST_CASE("list-extensions includes both Tube coprocessor plugins",
          "[integration][extension][list-extensions]") {
    // The 65C02 (3 MHz) and 65C102 (4 MHz) are two plugins built from the same
    // source; both appear from the default extensions directory.
    auto r = run_command(EXECUTABLE + " list-extensions");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    REQUIRE(r.stdout_output.find("tube-65c02") != std::string::npos);
    REQUIRE(r.stdout_output.find("tube-65c102") != std::string::npos);
}

TEST_CASE("list-extensions includes plugin extensions from the default directory",
          "[integration][extension][list-extensions]") {
    auto r = run_command(EXECUTABLE + " list-extensions");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    REQUIRE(r.stdout_output.find("acorn-rtc") != std::string::npos);
    REQUIRE(r.stdout_output.find("acorn-scsi") != std::string::npos);
}

TEST_CASE("list-extensions --format jsonl emits one JSON object per line",
          "[integration][extension][list-extensions]") {
    auto r = run_command(EXECUTABLE + " --format jsonl list-extensions");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    // Each line begins with '{' and ends with '}'
    std::istringstream iss(r.stdout_output);
    std::string line;
    int count = 0;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        REQUIRE(line.front() == '{');
        REQUIRE(line.back() == '}');
        ++count;
    }
    REQUIRE(count > 0);
}

TEST_CASE("list-extensions --extension-dir of a non-existent path errors",
          "[integration][extension][list-extensions]") {
    auto r = run_command(EXECUTABLE + " list-extensions --extension-dir /nonexistent-beebium-test-path");
    REQUIRE(r.exit_code != 0);
    INFO("stderr: " << r.stderr_output);
    REQUIRE(r.stderr_output.find("/nonexistent-beebium-test-path") != std::string::npos);
}

TEST_CASE("list-extensions --attaches-to serial-port shows only serial extensions",
          "[integration][extension][list-extensions][attachment-points]") {
    auto r = run_command(EXECUTABLE + " list-extensions --attaches-to serial-port");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    // The serial extensions appear...
    REQUIRE(r.stdout_output.find("host-serial") != std::string::npos);
    REQUIRE(r.stdout_output.find("ip232-serial") != std::string::npos);
    REQUIRE(r.stdout_output.find("rfc2217-server-serial") != std::string::npos);
    // ...and extensions for other points do not.
    REQUIRE(r.stdout_output.find("acorn-rtc") == std::string::npos);
    REQUIRE(r.stdout_output.find("acorn-scsi") == std::string::npos);
    REQUIRE(r.stdout_output.find("tube-65c02") == std::string::npos);
}

TEST_CASE("list-extensions output carries attaches_to (built-in and plugin manifests)",
          "[integration][extension][list-extensions][attachment-points]") {
    auto r = run_command(EXECUTABLE + " --format jsonl list-extensions");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    // host-serial is a built-in; tube-65c02 is the coprocessor plugin. host-serial's
    // attaches_to comes from BuiltinExtensions and tube-65c02's from its plugin
    // manifest, so this guards both sources.
    REQUIRE(r.stdout_output.find(
        "\"cli_name\":\"host-serial\"") != std::string::npos);
    REQUIRE(r.stdout_output.find("\"attaches_to\":[\"serial-port\"]") != std::string::npos);
    REQUIRE(r.stdout_output.find("\"attaches_to\":[\"tube\"]") != std::string::npos);
}

TEST_CASE("list-attachment-points lists the points with display names",
          "[integration][extension][list-attachment-points]") {
    auto r = run_command(EXECUTABLE + " --format pretty list-attachment-points");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    REQUIRE(r.stdout_output.find("serial-port") != std::string::npos);
    REQUIRE(r.stdout_output.find("Serial Port") != std::string::npos);
    REQUIRE(r.stdout_output.find("1 MHz Bus") != std::string::npos);
}

TEST_CASE("list-attachment-points --format jsonl carries occupancy bounds",
          "[integration][extension][list-attachment-points]") {
    auto r = run_command(EXECUTABLE + " --format jsonl list-attachment-points");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    // serial-port is bounded 0..1; the 1mhz-bus is unbounded (max null).
    REQUIRE(r.stdout_output.find(
        "\"id\":\"serial-port\",\"display_name\":\"Serial Port\",\"min_occupancy\":0,\"max_occupancy\":1")
        != std::string::npos);
    REQUIRE(r.stdout_output.find("\"max_occupancy\":null") != std::string::npos);
}

TEST_CASE("describe-extension shows parameter detail for a known extension",
          "[integration][extension][describe-extension]") {
    auto r = run_command(EXECUTABLE + " --format pretty describe-extension tube-65c02");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    REQUIRE(r.stdout_output.find("tube-65c02") != std::string::npos);
    REQUIRE(r.stdout_output.find("rom") != std::string::npos);  // parameter name
    // The synthesised invocation form tells the user how to invoke it.
    REQUIRE(r.stdout_output.find("Usage: --tube-65c02") != std::string::npos);
    // The client ROM the plugin ships is listed with its filename and size.
    REQUIRE(r.stdout_output.find("acorn-tube-6502_1_10.rom") != std::string::npos);
    REQUIRE(r.stdout_output.find("4096") != std::string::npos);
}

TEST_CASE("describe-extension shows the rom parameter for tube-65c102",
          "[integration][extension][describe-extension]") {
    auto r = run_command(EXECUTABLE + " --format pretty describe-extension tube-65c102");
    REQUIRE(r.exit_code == 0);
    INFO("stdout: " << r.stdout_output);
    REQUIRE(r.stdout_output.find("tube-65c102") != std::string::npos);
    REQUIRE(r.stdout_output.find("rom") != std::string::npos);
    REQUIRE(r.stdout_output.find("Usage: --tube-65c102") != std::string::npos);
    // The 65C102 ships its own distinct client ROM.
    REQUIRE(r.stdout_output.find("acorn-tube-65c102_1_20.rom") != std::string::npos);
    REQUIRE(r.stdout_output.find("4096") != std::string::npos);
}

TEST_CASE("starting with both coprocessor flags fails with the single-socket message",
          "[integration][extension][start]") {
    // The Tube has one socket, so two coprocessors cannot both attach. The
    // server refuses to start, before building anything, with a clear message.
    auto r = run_command(EXECUTABLE + " start --tube-65c02 --tube-65c102 --port 0 --wait");
    INFO("stdout: " << r.stdout_output);
    INFO("stderr: " << r.stderr_output);
    REQUIRE(r.exit_code == 78);
    REQUIRE(r.stderr_output.find("--tube-65c102: Tube holds at most 1 extension") !=
            std::string::npos);
}

TEST_CASE("describe-extension errors for unknown extension",
          "[integration][extension][describe-extension]") {
    auto r = run_command(EXECUTABLE + " describe-extension definitely-not-a-real-extension");
    REQUIRE(r.exit_code != 0);
    INFO("stderr: " << r.stderr_output);
    REQUIRE(r.stderr_output.find("definitely-not-a-real-extension") != std::string::npos);
}

TEST_CASE("describe-extension requires a name argument",
          "[integration][extension][describe-extension]") {
    auto r = run_command(EXECUTABLE + " describe-extension");
    REQUIRE(r.exit_code != 0);
}

TEST_CASE("start --help lists the tube-65c02 coprocessor plugin flag",
          "[integration][extension][start-help]") {
    auto r = run_command(EXECUTABLE + " start --help");
    INFO("stdout: " << r.stdout_output);
    INFO("stderr: " << r.stderr_output);
    auto combined = r.stdout_output + r.stderr_output;
    REQUIRE(combined.find("--tube-65c02") != std::string::npos);
}

TEST_CASE("start --help lists plugin extension flags from the default directory",
          "[integration][extension][start-help]") {
    auto r = run_command(EXECUTABLE + " start --help");
    INFO("stdout: " << r.stdout_output);
    INFO("stderr: " << r.stderr_output);
    auto combined = r.stdout_output + r.stderr_output;
    REQUIRE(combined.find("--acorn-scsi") != std::string::npos);
    REQUIRE(combined.find("--acorn-rtc") != std::string::npos);
    REQUIRE(combined.find("--scsi-hdd") != std::string::npos);
}

TEST_CASE("start --help mentions --extension-dir is repeatable",
          "[integration][extension][start-help]") {
    auto r = run_command(EXECUTABLE + " start --help");
    auto combined = r.stdout_output + r.stderr_output;
    INFO("combined: " << combined);
    REQUIRE(combined.find("--extension-dir") != std::string::npos);
    // Description should call out the repeatability/override semantics.
    REQUIRE((combined.find("Repeatable") != std::string::npos
             || combined.find("repeatable") != std::string::npos));
}

// ============================================================================
// describe-extension --json: everything a generic form needs
// ============================================================================

namespace {

nlohmann::json describe_extension_json(const std::string& name) {
    auto r = run_command(EXECUTABLE + " describe-extension " + name + " --json");
    INFO(r.stderr_output);
    REQUIRE(r.exit_code == 0);
    return nlohmann::json::parse(r.stdout_output);
}

const nlohmann::json& parameter(const nlohmann::json& ext, const std::string& key) {
    for (const auto& p : ext.at("parameters")) {
        if (p.at("key") == key) return p;
    }
    FAIL("no parameter " << key);
    static nlohmann::json none;
    return none;
}

}  // namespace

TEST_CASE("describe-extension --json describes the extension and each parameter",
          "[integration][extension][describe-extension][json]") {
    auto ext = describe_extension_json("scsi-hdd");
    CHECK(ext.at("name") == "scsi-hard-disc");
    CHECK(ext.at("cli_name") == "scsi-hdd");
    CHECK(ext.at("display_name") == "SCSI Hard Disc");
    CHECK(ext.at("kind") == "peripheral");
    CHECK(ext.at("attaches_to") == nlohmann::json::array({"scsi"}));
    CHECK(ext.at("provides") == nlohmann::json::array());
    const auto& scsi_id = parameter(ext, "scsi-id");
    CHECK(scsi_id.at("type") == "integer");
    CHECK(scsi_id.at("min") == 0);
    CHECK(scsi_id.at("max") == 7);
    CHECK(scsi_id.at("default") == "0");
    CHECK(scsi_id.at("unique") == true);
    CHECK_FALSE(scsi_id.at("label").get<std::string>().empty());
    CHECK(parameter(ext, "image").at("bundled") == "hard-disc");
    CHECK(parameter(ext, "image").at("min").is_null());
}

TEST_CASE("describe-extension --json lists a parameter's choices",
          "[integration][extension][describe-extension][json]") {
    const auto aun = describe_extension_json("aun");
    const auto& discovery = parameter(aun, "discovery");
    std::vector<std::string> values;
    for (const auto& c : discovery.at("choices")) {
        values.push_back(c.at("value").get<std::string>());
        CHECK_FALSE(c.at("label").get<std::string>().empty());
    }
    CHECK(values == std::vector<std::string>{"on", "announce", "browse", "off"});
}

TEST_CASE("every parameter of every shipped extension has a label and a description",
          "[integration][extension][describe-extension][json]") {
    auto list = run_command(EXECUTABLE + " --format jsonl list-extensions");
    REQUIRE(list.exit_code == 0);
    std::istringstream lines(list.stdout_output);
    std::string line;
    int extensions = 0;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        auto name = nlohmann::json::parse(line).at("name").get<std::string>();
        auto ext = describe_extension_json(name);
        ++extensions;
        INFO(name);
        CHECK_FALSE(ext.at("display_name").get<std::string>().empty());
        CHECK_FALSE(ext.at("description").get<std::string>().empty());
        for (const auto& p : ext.at("parameters")) {
            INFO(p.at("key"));
            CHECK_FALSE(p.at("label").get<std::string>().empty());
            CHECK_FALSE(p.at("description").get<std::string>().empty());
        }
    }
    CHECK(extensions >= 10);
}

TEST_CASE("start refuses an out-of-range SCSI ID as a usage error",
          "[integration][extension][start][bounds]") {
    auto r = run_command(EXECUTABLE + " start --acorn-scsi --scsi-hdd 8:/tmp/x.dat --port 0");
    CHECK(r.exit_code == 64);
    CHECK(r.stderr_output.find("must be 0-7") != std::string::npos);
}

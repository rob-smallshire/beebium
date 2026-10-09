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

// Integration tests for preset management subcommands
//
// These tests run the actual executables as subprocesses to verify
// end-to-end behavior of the preset management workflow.

#include <catch2/catch_test_macros.hpp>
#include <beebium/PlatformUtils.hpp>
#include <beebium/server/PresetLoader.hpp>
#include "stb_image.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "test_temp_paths.hpp"

namespace {

// Helper to run a command and capture stdout/stderr
struct ProcessResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

// Cross-platform environment variable helpers
// Set environment variable in current process (inherited by child processes)
void set_env(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

// Unset environment variable in current process
void unset_env(const std::string& name) {
#ifdef _WIN32
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

// Best-effort removal of a temporary file produced by a test. On Windows a
// file that was just written by a subprocess can be briefly held open by an
// external scanner (e.g. Defender or the search indexer), so an immediate
// delete may fail with "being used by another process". The file is in the
// temp directory and harmless if left behind, so cleanup must never fail the
// test. This mirrors TempDirectory's error_code-based remove_all().
void remove_quietly(const std::filesystem::path& filepath) {
    std::error_code ec;
    std::filesystem::remove(filepath, ec);
}

ProcessResult run_command(const std::string& command,
                          const std::vector<std::pair<std::string, std::string>>& env_vars = {}) {
    ProcessResult result;
    result.exit_code = -1;

    // Create temporary files for output capture
    auto stdout_filepath = beebium::test::unique_temp_path("beebium_test_stdout_", ".txt");
    auto stderr_filepath = beebium::test::unique_temp_path("beebium_test_stderr_", ".txt");

    // Set environment variables in current process - child processes inherit them
    for (const auto& [name, value] : env_vars) {
        set_env(name, value);
    }

    // Build command with output redirection
    std::string full_command = command;
#ifdef _WIN32
    full_command += " >\"" + stdout_filepath.string() + "\" 2>\"" + stderr_filepath.string() + "\"";
#else
    full_command += " >" + stdout_filepath.string() + " 2>" + stderr_filepath.string();
#endif

    result.exit_code = std::system(full_command.c_str());

    // Unset environment variables after command completes
    for (const auto& [name, value] : env_vars) {
        unset_env(name);
    }

    // WEXITSTATUS macro to get actual exit code on Unix
#ifdef _WIN32
    // On Windows, system() returns the exit code directly
#else
    if (WIFEXITED(result.exit_code)) {
        result.exit_code = WEXITSTATUS(result.exit_code);
    }
#endif

    // Read stdout
    if (std::ifstream f(stdout_filepath); f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        result.stdout_output = ss.str();
    }

    // Read stderr
    if (std::ifstream f(stderr_filepath); f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        result.stderr_output = ss.str();
    }

    // Clean up temp files
    std::filesystem::remove(stdout_filepath);
    std::filesystem::remove(stderr_filepath);

    return result;
}

// Get path to executable - looks in common locations
std::filesystem::path find_executable(const std::string& name) {
#ifdef _WIN32
    std::string exe_name = name + ".exe";
#else
    std::string exe_name = name;
#endif

    // Check environment variable first
    if (auto env = beebium::platform::get_env("BEEBIUM_SERVERS_DIRPATH")) {
        auto path = std::filesystem::path(*env) / exe_name;
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    // Check relative to build directory (typical CMake layout)
    std::vector<std::filesystem::path> search_paths = {
        std::filesystem::current_path() / "src" / "server" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / exe_name,
        std::filesystem::current_path() / "build" / "src" / "server" / exe_name,
#ifdef _WIN32
        // MSVC puts executables in Release/Debug subdirectories
        std::filesystem::current_path() / "src" / "server" / "Release" / exe_name,
        std::filesystem::current_path() / "src" / "server" / "Debug" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / "Release" / exe_name,
        std::filesystem::current_path() / ".." / "src" / "server" / "Debug" / exe_name,
#endif
    };

    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }

    return exe_name;  // Fall back to just the name (rely on PATH)
}

// RAII helper for temporary directories
class TempDirectory : public beebium::test::ScopedTempDir {
public:
    TempDirectory() : ScopedTempDir("beebium_test_") {}
};

const std::string EXECUTABLE = find_executable("beebium-model-b").string();

}  // namespace

// ============================================================================
// list-presets integration tests
// ============================================================================

TEST_CASE("list-presets: returns OK", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets");

    REQUIRE(result.exit_code == 0);
}

TEST_CASE("list-presets --json: outputs valid JSON array", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets --json");

    REQUIRE(result.exit_code == 0);
    // Basic JSON structure check - should start with {"presets": or similar
    REQUIRE_FALSE(result.stdout_output.empty());
    REQUIRE((result.stdout_output.find('[') != std::string::npos ||
             result.stdout_output.find('{') != std::string::npos));
}

namespace {

// A 30-character preset id, longer than any fixed-width id column.
const std::string LONG_PRESET_ID = "a-thirty-character-preset-id-x";

void write_long_id_user_preset(const std::filesystem::path& user_dirpath) {
    std::ofstream out(user_dirpath / (LONG_PRESET_ID + ".preset.beebium"));
    out << R"({"model": "model-b", "name": "Long Id Preset"})";
}

}  // namespace

TEST_CASE("list-presets: pretty id column leaves a gap after the longest id", "[integration][preset][list-presets]") {
    REQUIRE(LONG_PRESET_ID.size() == 30);
    TempDirectory temp_dir;
    write_long_id_user_preset(temp_dir.path());

    auto result = run_command(EXECUTABLE + " --format pretty list-presets",
                              {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    INFO(result.stdout_output);
    REQUIRE(result.stdout_output.find("  " + LONG_PRESET_ID + "  Long Id Preset\n") != std::string::npos);
}

TEST_CASE("list-presets: --format tsv yields tab-separated rows", "[integration][preset][list-presets]") {
    TempDirectory temp_dir;
    write_long_id_user_preset(temp_dir.path());

    auto result = run_command(EXECUTABLE + " --format tsv list-presets",
                              {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    INFO(result.stdout_output);
    REQUIRE(result.stdout_output.rfind("id\tname\tsource\n", 0) == 0);
    REQUIRE(result.stdout_output.find("\nmodel-b\t") != std::string::npos);
    REQUIRE(result.stdout_output.find("\n" + LONG_PRESET_ID + "\tLong Id Preset\tuser\n") != std::string::npos);
}

TEST_CASE("list-presets: --format jsonl and --json yield the same rows", "[integration][preset][list-presets]") {
    TempDirectory temp_dir;
    write_long_id_user_preset(temp_dir.path());
    std::vector<std::pair<std::string, std::string>> env{
        {"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}};

    auto jsonl = run_command(EXECUTABLE + " --format jsonl list-presets", env);
    auto json = run_command(EXECUTABLE + " list-presets --json", env);

    REQUIRE(jsonl.exit_code == 0);
    REQUIRE(json.exit_code == 0);
    REQUIRE(jsonl.stdout_output == json.stdout_output);
    INFO(jsonl.stdout_output);
    REQUIRE(jsonl.stdout_output.find(
        "{\"id\":\"" + LONG_PRESET_ID + "\",\"name\":\"Long Id Preset\",\"source\":\"user\"}\n")
        != std::string::npos);
}

TEST_CASE("list-presets --help: returns OK", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// report-presets-dirpath integration tests
// ============================================================================

TEST_CASE("report-presets-dirpath: returns a path", "[integration][preset][report-presets-dirpath]") {
    auto result = run_command(EXECUTABLE + " report-presets-dirpath");

    REQUIRE(result.exit_code == 0);
    REQUIRE_FALSE(result.stdout_output.empty());
    // Should contain a path separator
    REQUIRE((result.stdout_output.find('/') != std::string::npos ||
             result.stdout_output.find('\\') != std::string::npos));
}

TEST_CASE("report-presets-dirpath: respects BEEBIUM_USER_PRESETS_DIRPATH", "[integration][preset][report-presets-dirpath]") {
    TempDirectory temp_dir;

    auto result = run_command(
        EXECUTABLE + " report-presets-dirpath",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find(temp_dir.path().string()) != std::string::npos);
}

// ============================================================================
// show-preset integration tests
// ============================================================================

TEST_CASE("show-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][show-preset]") {
    auto result = run_command(EXECUTABLE + " show-preset nonexistent-preset-12345");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("show-preset --help: returns OK", "[integration][preset][show-preset]") {
    auto result = run_command(EXECUTABLE + " show-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// create-preset integration tests
// ============================================================================

TEST_CASE("create-preset: missing --name returns USAGE (64)", "[integration][preset][create-preset]") {
    auto result = run_command(EXECUTABLE + " create-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("create-preset --output: creates preset file", "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "test-preset.preset.beebium";

    auto result = run_command(EXECUTABLE + " create-preset --name \"Test Preset\" --output \"" +
                              output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    // Verify content
    std::ifstream f(output_filepath);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("\"model\": \"model-b\"") != std::string::npos);
    REQUIRE(content.find("\"name\": \"Test Preset\"") != std::string::npos);
}

TEST_CASE("create-preset: creates preset in user directory", "[integration][preset][create-preset]") {
    TempDirectory temp_dir;

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"My Test Setup\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    // Output should be the preset ID
    REQUIRE(result.stdout_output.find("my-test-setup") != std::string::npos);

    // Verify file was created
    auto preset_filepath = temp_dir.path() / "my-test-setup.preset.beebium";
    REQUIRE(std::filesystem::exists(preset_filepath));
}

TEST_CASE("create-preset: captures a serial extension that round-trips through the loader",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "serial.preset.beebium";

    // create-preset accepts the same --<name> spec flags as `start`.
    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Serial Box\" --output \"" +
        output_filepath.string() + "\" --rpc-serial tx_buffer=256");
    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    // Round-trip: PresetLoader parses exactly what create-preset emitted.
    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    REQUIRE(loaded.config->extensions.size() == 1);
    REQUIRE(loaded.config->extensions[0].name == "rpc-serial");
    REQUIRE(loaded.config->extensions[0].config.at("tx_buffer") == "256");
}

TEST_CASE("create-preset: --station + --aun round-trips to an econet transport",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "aun.preset.beebium";

    // An econet-transport extension flag is recorded in the preset's canonical
    // econet.transport section (not the generic extensions array).
    auto result = run_command(
        EXECUTABLE + " create-preset --name \"AUN Box\" --output \"" +
        output_filepath.string() + "\" --station 80 --aun");
    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    REQUIRE(loaded.config->econet.has_value());
    CHECK(loaded.config->econet->station == 80);
    REQUIRE(loaded.config->econet->transport.has_value());
    CHECK(loaded.config->econet->transport->name == "aun");
    // Only what was given is recorded: AUN's defaults stay defaults.
    CHECK(loaded.config->econet->transport->parameters.empty());
    // The transport must NOT also leak into the generic extensions array.
    CHECK(loaded.config->extensions.empty());
}

TEST_CASE("create-preset: --station + --piconet omits a discovered device_path",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "piconet.preset.beebium";

    // No device_path given: the transport records name only, so the extension
    // discovers the device at boot (device_path's default, "auto").
    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Piconet Box\" --output \"" +
        output_filepath.string() + "\" --station 81 --piconet");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    REQUIRE(loaded.config->econet.has_value());
    CHECK(loaded.config->econet->station == 81);
    REQUIRE(loaded.config->econet->transport.has_value());
    CHECK(loaded.config->econet->transport->name == "piconet");
    CHECK(loaded.config->econet->transport->parameters.empty());
}

TEST_CASE("create-preset: --machine-name round-trips through the loader",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "named.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Server\" --machine-name \"L3FS\" "
        "--output \"" + output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    REQUIRE(loaded.config->machine_name.has_value());
    CHECK(*loaded.config->machine_name == "L3FS");
}

TEST_CASE("create-preset: a --machine-name template round-trips unchanged (#153)",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "templated.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Station\" "
        "--machine-name \"Station {econet-station} {{x}}\" "
        "--output \"" + output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    REQUIRE(loaded.config->machine_name.has_value());
    CHECK(*loaded.config->machine_name == "Station {econet-station} {{x}}");
}

TEST_CASE("create-preset: --auto-boot round-trips through the loader",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "autoboot.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Server\" --auto-boot "
        "--output \"" + output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    CHECK(loaded.warnings().empty());  // written as startup.auto_boot
    REQUIRE(loaded.config->startup.has_value());
    CHECK(loaded.config->startup->auto_boot == true);
}

TEST_CASE("create-preset: without --auto-boot the preset omits it",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "noboot.preset.beebium";
    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Plain\" --output \"" +
        output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);
    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    CHECK_FALSE(loaded.config->startup.has_value());
}

TEST_CASE("create-preset: machine settings round-trip through the loader",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "settings.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Settings\" --speed 2 --screen-mode 3 "
        "--mos my-mos.rom --fdc acorn-1770 --floppy 0:discs/game.ssd "
        "--output \"" + output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    CHECK(loaded.warnings().empty());
    CHECK(loaded.config->speed == 2.0);
    REQUIRE(loaded.config->startup.has_value());
    CHECK(loaded.config->startup->screen_mode == 3);
    CHECK(loaded.config->mos_image_uri == "my-mos.rom");
    REQUIRE(loaded.config->storage.has_value());
    REQUIRE(loaded.config->storage->floppy_drives.at(0).has_value());
    // A relative --floppy path means what it means to `start`: relative to the
    // working directory, so the preset records it absolutely.
    CHECK(*loaded.config->storage->floppy_drives.at(0) ==
          "file://" + (std::filesystem::current_path() / "discs" / "game.ssd")
                          .lexically_normal().generic_string());
}

TEST_CASE("create-preset: --links and --language-rom round-trip through the loader",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "links.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Links\" --links 135 --speed unlimited "
        "--language-rom forth.rom --output \"" + output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    CHECK(loaded.config->startup->links == 135);
    CHECK(loaded.config->speed == 0.0);
    // The language ROM is the model's language slot (15 on a Model B).
    REQUIRE(loaded.config->sideways.size() == 1);
    CHECK(loaded.config->sideways[0].slot == 15);
    CHECK(loaded.config->sideways[0].image_filepath == "forth.rom");
}

TEST_CASE("create-preset: --links with --screen-mode is a usage error",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "bad.preset.beebium";
    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Bad\" --links 7 --screen-mode 3 "
        "--output \"" + output_filepath.string() + "\"");
    CHECK(result.exit_code == 64);
    CHECK_FALSE(std::filesystem::exists(output_filepath));
}

TEST_CASE("create-preset: --station without a transport fits Econet with no "
          "network",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "station-only.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Station Only\" --output \"" +
        output_filepath.string() + "\" --station 42");
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    REQUIRE(loaded.config->econet.has_value());
    CHECK(loaded.config->econet->station == 42);
    CHECK_FALSE(loaded.config->econet->transport.has_value());
}

TEST_CASE("create-preset: a transport without --station is rejected",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "no-station.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"No Station\" --output \"" +
        output_filepath.string() + "\" --aun");
    CHECK(result.exit_code == 64);  // ExitCode::USAGE
    CHECK_FALSE(std::filesystem::exists(output_filepath));
}

TEST_CASE("create-preset: --station out of range is rejected",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "bad-station.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"Bad Station\" --output \"" +
        output_filepath.string() + "\" --station 300 --aun");
    CHECK(result.exit_code == 64);  // ExitCode::USAGE
}

TEST_CASE("create-preset --help: returns OK", "[integration][preset][create-preset]") {
    auto result = run_command(EXECUTABLE + " create-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// delete-preset integration tests
// ============================================================================

TEST_CASE("delete-preset: missing ID returns USAGE (64)", "[integration][preset][delete-preset]") {
    auto result = run_command(EXECUTABLE + " delete-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("delete-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][delete-preset]") {
    auto result = run_command(EXECUTABLE + " delete-preset nonexistent-preset-12345");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("delete-preset: deletes user preset", "[integration][preset][delete-preset]") {
    TempDirectory temp_dir;

    // First create a preset
    auto preset_filepath = temp_dir.path() / "deleteme.preset.beebium";
    {
        std::ofstream f(preset_filepath);
        f << R"({"model": "model-b", "name": "Delete Me"})";
    }
    REQUIRE(std::filesystem::exists(preset_filepath));

    // Delete it
    auto result = run_command(
        EXECUTABLE + " delete-preset deleteme",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE_FALSE(std::filesystem::exists(preset_filepath));
}

TEST_CASE("delete-preset --help: returns OK", "[integration][preset][delete-preset]") {
    auto result = run_command(EXECUTABLE + " delete-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// import-preset integration tests
// ============================================================================

TEST_CASE("import-preset: missing filepath returns USAGE (64)", "[integration][preset][import-preset]") {
    auto result = run_command(EXECUTABLE + " import-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("import-preset: nonexistent file returns NOINPUT (66)", "[integration][preset][import-preset]") {
    auto result = run_command(EXECUTABLE + " import-preset /nonexistent/path.preset.beebium");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("import-preset: imports valid preset", "[integration][preset][import-preset]") {
    TempDirectory source_dir;
    TempDirectory dest_dir;

    // Create source preset
    auto source_filepath = source_dir.path() / "importme.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"model": "model-b", "name": "Import Me"})";
    }

    auto result = run_command(
        EXECUTABLE + " import-preset \"" + source_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", dest_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("importme") != std::string::npos);

    // Verify file was created in dest directory
    auto imported_filepath = dest_dir.path() / "importme.preset.beebium";
    REQUIRE(std::filesystem::exists(imported_filepath));
}

TEST_CASE("import-preset: rejects wrong model", "[integration][preset][import-preset]") {
    TempDirectory temp_dir;

    // Create preset with wrong model
    auto source_filepath = temp_dir.path() / "wrong-model.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"model": "wrong-model", "name": "Wrong Model"})";
    }

    auto result = run_command(EXECUTABLE + " import-preset \"" + source_filepath.string() + "\"");

    REQUIRE(result.exit_code == 65);  // DATAERR
    REQUIRE(result.stderr_output.find("does not match") != std::string::npos);
}

TEST_CASE("import-preset --help: returns OK", "[integration][preset][import-preset]") {
    auto result = run_command(EXECUTABLE + " import-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// export-preset integration tests
// ============================================================================

TEST_CASE("export-preset: missing ID returns USAGE (64)", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset --output /tmp/out.preset.beebium");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("export-preset: missing --output returns USAGE (64)", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset some-id");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("export-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][export-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "exported.preset.beebium";

    auto result = run_command(EXECUTABLE + " export-preset nonexistent-12345 --output \"" +
                              output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("export-preset: exports user preset", "[integration][preset][export-preset]") {
    TempDirectory user_dir;
    TempDirectory output_dir;

    // Create source preset in user directory
    auto source_filepath = user_dir.path() / "mypreset.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"model": "model-b", "name": "My Preset"})";
    }

    auto output_filepath = output_dir.path() / "exported.preset.beebium";

    auto result = run_command(
        EXECUTABLE + " export-preset mypreset --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    // Verify content matches
    std::ifstream f(output_filepath);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    REQUIRE(content.find("\"name\": \"My Preset\"") != std::string::npos);
}

TEST_CASE("export-preset --help: returns OK", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// End-to-end workflow tests
// ============================================================================

TEST_CASE("Full preset workflow: create, export, import, delete", "[integration][preset][workflow]") {
    TempDirectory user_dir;
    TempDirectory export_dir;
    TempDirectory import_dir;

    // Step 1: Create a preset
    auto create_result = run_command(
        EXECUTABLE + " create-preset --name \"Workflow Test\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(create_result.exit_code == 0);

    std::string preset_id = create_result.stdout_output;
    // Trim whitespace
    while (!preset_id.empty() && (preset_id.back() == '\n' || preset_id.back() == '\r')) {
        preset_id.pop_back();
    }
    REQUIRE_FALSE(preset_id.empty());

    // Verify preset file exists
    auto preset_filepath = user_dir.path() / (preset_id + ".preset.beebium");
    REQUIRE(std::filesystem::exists(preset_filepath));

    // Step 2: Export the preset
    auto export_filepath = export_dir.path() / "exported.preset.beebium";
    auto export_result = run_command(
        EXECUTABLE + " export-preset " + preset_id + " --output \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(export_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(export_filepath));

    // Step 3: Delete the original
    auto delete_result = run_command(
        EXECUTABLE + " delete-preset " + preset_id,
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(delete_result.exit_code == 0);
    REQUIRE_FALSE(std::filesystem::exists(preset_filepath));

    // Step 4: Import from export
    auto import_result = run_command(
        EXECUTABLE + " import-preset \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", import_dir.path().string()}}
    );
    REQUIRE(import_result.exit_code == 0);

    // Verify imported file exists
    auto imported_filepath = import_dir.path() / "exported.preset.beebium";
    REQUIRE(std::filesystem::exists(imported_filepath));
}

// ============================================================================
// describe-preset-schema sideways_bank section
// ============================================================================

TEST_CASE("describe-preset-schema: includes sideways_bank section",
          "[integration][preset][describe-preset-schema][sideways]") {
    auto result = run_command(EXECUTABLE + " describe-preset-schema");

    REQUIRE(result.exit_code == 0);
    // The sideways bank section drives the frontend Memory tab.
    REQUIRE(result.stdout_output.find("\"sideways_bank\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"sockets\"") != std::string::npos);
    // Default ROM placements are advertised so the UI can show what is already
    // in a slot; Model B ships BASIC in the language slot.
    REQUIRE(result.stdout_output.find("\"default_roms\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("bbc-basic_2.rom") != std::string::npos);
    // Model B's aliased sockets are labelled by IC designation.
    REQUIRE(result.stdout_output.find("IC52") != std::string::npos);
}

// ============================================================================
// sideways_bank round-trip through create-preset / export-preset
// ============================================================================

TEST_CASE("create-preset --from and export-preset preserve sideways_bank",
          "[integration][preset][create-preset][export-preset][sideways]") {
    TempDirectory user_dir;

    // A source preset carrying a sideways_bank DFS slot.
    auto source_filepath = user_dir.path() / "sideways-src.preset.beebium";
    {
        std::ofstream f(source_filepath);
        f << R"({"name": "Sideways Source", "model": "model-b", )"
             R"("sideways_bank": { "slots": [ )"
             R"({ "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" } ] }})";
    }

    auto read_file = [](const std::filesystem::path& p) {
        std::ifstream f(p);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    // Copy via create-preset --from: the section should survive the copy.
    auto create_result = run_command(
        EXECUTABLE + " create-preset --from sideways-src --name \"Sideways Copy\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(create_result.exit_code == 0);

    std::string new_id = create_result.stdout_output;
    while (!new_id.empty() &&
           (new_id.back() == '\n' || new_id.back() == '\r' || new_id.back() == ' ')) {
        new_id.pop_back();
    }
    REQUIRE_FALSE(new_id.empty());

    auto copy_filepath = user_dir.path() / (new_id + ".preset.beebium");
    REQUIRE(std::filesystem::exists(copy_filepath));
    std::string copy_contents = read_file(copy_filepath);
    REQUIRE(copy_contents.find("sideways_bank") != std::string::npos);
    REQUIRE(copy_contents.find("acorn-dfs_2_26.rom") != std::string::npos);

    // Export the copy: the section should survive export too.
    auto export_filepath = user_dir.path() / "exported-sideways.preset.beebium";
    auto export_result = run_command(
        EXECUTABLE + " export-preset " + new_id + " --output \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(export_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(export_filepath));
    std::string export_contents = read_file(export_filepath);
    REQUIRE(export_contents.find("sideways_bank") != std::string::npos);
    REQUIRE(export_contents.find("acorn-dfs_2_26.rom") != std::string::npos);
}

TEST_CASE("create-preset --from: --sideways replaces only the slots it names",
          "[integration][preset][create-preset][sideways]") {
    TempDirectory user_dir;
    {
        std::ofstream f(user_dir.path() / "slots-src.preset.beebium");
        f << R"({"name": "Slots Source", "model": "model-b", "auto_boot": true, )"
             R"("sideways_bank": { "slots": [ )"
             R"({ "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" }, )"
             R"({ "slot": 13, "type": "empty" } ] }})";
    }
    auto output_filepath = user_dir.path() / "slots-copy.preset.beebium";
    auto result = run_command(
        EXECUTABLE + " create-preset --from slots-src --name \"Slots Copy\" "
        "--sideways slot=13:type=ram --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}});
    REQUIRE(result.exit_code == 0);

    auto loaded = beebium::server::load_preset(output_filepath);
    REQUIRE(loaded.success());
    CHECK(loaded.warnings().empty());  // the source's top-level auto_boot moved into startup
    CHECK(loaded.config->startup->auto_boot == true);
    REQUIRE(loaded.config->sideways.size() == 2);
    auto slot = [&](int n) {
        for (const auto& c : loaded.config->sideways) {
            if (c.slot == n) return c;
        }
        FAIL("no slot " << n);
        return beebium::server::SidewaysConfig{};
    };
    CHECK(slot(14).type == beebium::server::SidewaysSlotType::Rom);  // kept from the source
    CHECK(slot(13).type == beebium::server::SidewaysSlotType::Ram);  // replaced
}

// ============================================================================
// model-b-romram no longer auto-provisions DFS
// ============================================================================

TEST_CASE("model-b-romram boots without an auto-loaded DFS ROM",
          "[integration][romram][sideways]") {
    const std::string romram = find_executable("beebium-model-b-romram").string();
    auto output_filepath = beebium::test::unique_temp_path("beebium_romram_bare_", ".png");

    auto result = run_command(
        romram + " capture-screenshot --output \"" + output_filepath.string() + "\" --duration 0");
    REQUIRE(result.exit_code == 0);

    // BASIC still auto-loads: the language ROM ships in the machine.
    REQUIRE(result.stdout_output.find("bbc-basic_2.rom") != std::string::npos);
    // But DFS is no longer auto-provisioned for a ROM/RAM board; it must come
    // from a preset's sideways_bank. Nothing should reference acorn-dfs here.
    REQUIRE(result.stdout_output.find("acorn-dfs") == std::string::npos);

    remove_quietly(output_filepath);
}

// ============================================================================
// create-preset builds rich presets from --fdc / --sideways flags
// ============================================================================

TEST_CASE("create-preset --fdc and --sideways build a loadable rich preset",
          "[integration][preset][create-preset][sideways]") {
    auto output_filepath = beebium::test::unique_temp_path("beebium_gen_disc_", ".preset.beebium");

    auto create_result = run_command(
        EXECUTABLE + " create-preset --name \"Gen Disc\" --release-date 1982"
        " --fdc acorn-1770 --sideways slot=14:type=rom:image=acorn-dfs_2_26.rom"
        " --output \"" + output_filepath.string() + "\"");
    REQUIRE(create_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    std::ifstream f(output_filepath);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string contents = ss.str();
    REQUIRE(contents.find("\"acorn-1770\"") != std::string::npos);
    REQUIRE(contents.find("sideways_bank") != std::string::npos);
    REQUIRE(contents.find("acorn-dfs_2_26.rom") != std::string::npos);
    REQUIRE(contents.find("1982") != std::string::npos);

    // The generated preset must load, validate, and boot end to end.
    auto screenshot_filepath = beebium::test::unique_temp_path("beebium_gen_disc_", ".png");
    auto boot_result = run_command(
        EXECUTABLE + " capture-screenshot --preset \"" + output_filepath.string() +
        "\" --output \"" + screenshot_filepath.string() + "\" --duration 0");
    REQUIRE(boot_result.exit_code == 0);
    REQUIRE(boot_result.stdout_output.find("acorn-dfs_2_26.rom") != std::string::npos);

    remove_quietly(output_filepath);
    remove_quietly(screenshot_filepath);
}

// ============================================================================
// shipped system presets load, validate, and boot
// ============================================================================

TEST_CASE("shipped system presets load, validate, and boot",
          "[integration][preset][shipped]") {
    struct PresetCase {
        std::string executable;
        std::string preset_id;
        bool expect_dfs;  // disc-equipped (preset) or integral (B+)
    };
    const std::vector<PresetCase> cases = {
        {"beebium-model-b",        "model-b",             false},  // bare cassette
        {"beebium-model-b",        "model-b-disc",        true},   // 1770 + DFS via preset
        {"beebium-model-b-romram", "model-b-romram-disc", true},   // 1770 + DFS via preset
        {"beebium-model-b-plus",   "model-b-plus",        true},   // integral DFS
    };

    for (const auto& c : cases) {
        auto executable = find_executable(c.executable);
        auto preset_filepath =
            executable.parent_path() / "presets" / (c.preset_id + ".preset.beebium");
        INFO("preset: " << c.preset_id);
        REQUIRE(std::filesystem::exists(preset_filepath));

        auto screenshot_filepath =
            beebium::test::unique_temp_path("beebium_shipped_" + c.preset_id + "_", ".png");
        auto result = run_command(
            executable.string() + " capture-screenshot --preset \"" +
            preset_filepath.string() + "\" --output \"" + screenshot_filepath.string() +
            "\" --duration 0");
        INFO("stderr: " << result.stderr_output);
        REQUIRE(result.exit_code == 0);
        if (c.expect_dfs) {
            REQUIRE(result.stdout_output.find("acorn-dfs_2_26.rom") != std::string::npos);
        }
        remove_quietly(screenshot_filepath);
    }
}

// ============================================================================
// capture-screenshot runs the coprocessor (one machine assembly for every
// subcommand). Release-blocking regression: capture-screenshot used to build
// its own extension-less machine, so a Tube preset rendered the host banner --
// its thumbnail was byte-identical to the non-coprocessor twin. Now it goes
// through the same assemble_machine as `start`, so the coprocessor boots and
// the rendered screen differs from the host twin.
// ============================================================================

TEST_CASE("capture-screenshot runs the Tube coprocessor from a preset",
          "[integration][preset][screenshot][tube]") {
    struct TubeCase {
        std::string executable;
        std::string tube_preset;   // coprocessor preset id
        std::string host_preset;   // its non-coprocessor twin
        std::string extension;     // extension name expected in stdout
    };
    const std::vector<TubeCase> cases = {
        {"beebium-model-b",          "model-b-disc-65c02-copro",
         "model-b-disc",             "acorn-65c02-coprocessor"},
        {"beebium-model-b-plus-128k", "model-b-plus-128k-65c102-copro",
         "model-b-plus-128k",        "acorn-65c102-coprocessor"},
    };

    auto read_file = [](const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };

    for (const auto& c : cases) {
        auto executable = find_executable(c.executable);
        auto presets_dir = executable.parent_path() / "presets";
        auto tube_preset = presets_dir / (c.tube_preset + ".preset.beebium");
        auto host_preset = presets_dir / (c.host_preset + ".preset.beebium");
        INFO("tube preset: " << c.tube_preset);
        REQUIRE(std::filesystem::exists(tube_preset));
        REQUIRE(std::filesystem::exists(host_preset));

        auto tube_png = beebium::test::unique_temp_path("beebium_" + c.tube_preset + "_", ".png");
        auto host_png =
            beebium::test::unique_temp_path("beebium_" + c.host_preset + "_twin_", ".png");

        // Capture both at the same duration, past the boot banner, so the only
        // difference between the two renders is the coprocessor.
        auto tube_result = run_command(
            executable.string() + " capture-screenshot --preset \"" +
            tube_preset.string() + "\" --output \"" + tube_png.string() +
            "\" --duration 3");
        INFO("tube stderr: " << tube_result.stderr_output);
        REQUIRE(tube_result.exit_code == 0);

        // The screenshot path assembled and initialised the coprocessor.
        REQUIRE(tube_result.stdout_output.find(c.extension) != std::string::npos);
        REQUIRE(tube_result.stdout_output.find("coprocessor") != std::string::npos);

        auto host_result = run_command(
            executable.string() + " capture-screenshot --preset \"" +
            host_preset.string() + "\" --output \"" + host_png.string() +
            "\" --duration 3");
        INFO("host stderr: " << host_result.stderr_output);
        REQUIRE(host_result.exit_code == 0);

        // The coprocessor changes the rendered screen: the two PNGs must differ.
        // (Before the fix they were byte-identical.)
        const std::string tube_bytes = read_file(tube_png);
        const std::string host_bytes = read_file(host_png);
        REQUIRE_FALSE(tube_bytes.empty());
        REQUIRE_FALSE(host_bytes.empty());
        CHECK(tube_bytes != host_bytes);

        remove_quietly(tube_png);
        remove_quietly(host_png);
    }
}

// ============================================================================
// capture-screenshot --crop auto
// ============================================================================

namespace {

// Fraction of a PNG (loaded RGBA) occupied by the non-black content bounding
// box: (bbox area) / (image area). A pixel is content if any of R,G,B > 16 (the
// black border and void do not count). Returns -1 on load failure.
double content_fraction(const std::filesystem::path& png) {
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(png.string().c_str(), &w, &h, &channels, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return -1.0;
    }
    int min_x = w, min_y = h, max_x = -1, max_y = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = data + (static_cast<size_t>(y) * w + x) * 4;
            if (p[0] > 16 || p[1] > 16 || p[2] > 16) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }
    stbi_image_free(data);
    if (max_x < 0) return 0.0;  // all black
    const double bbox = static_cast<double>(max_x - min_x + 1)
                      * static_cast<double>(max_y - min_y + 1);
    return bbox / (static_cast<double>(w) * h);
}

}  // namespace

TEST_CASE("capture-screenshot --crop none is byte-identical to the default and to master",
          "[integration][screenshot][crop]") {
    // --crop none and no --crop flag must produce identical bytes (the default
    // is none). Existing output is unchanged; the master baseline is verified
    // separately in the branch's acceptance.
    auto exe = find_executable("beebium-model-b");
    auto preset = exe.parent_path() / "presets" / "model-b-disc.preset.beebium";
    REQUIRE(std::filesystem::exists(preset));

    auto none_png = beebium::test::unique_temp_path("beebium_crop_none_", ".png");
    auto def_png = beebium::test::unique_temp_path("beebium_crop_default_", ".png");

    auto none = run_command(exe.string() + " capture-screenshot --preset \"" +
                            preset.string() + "\" --output \"" + none_png.string() +
                            "\" --crop none --duration 3");
    auto def = run_command(exe.string() + " capture-screenshot --preset \"" +
                           preset.string() + "\" --output \"" + def_png.string() +
                           "\" --duration 3");
    REQUIRE(none.exit_code == 0);
    REQUIRE(def.exit_code == 0);

    auto read = [](const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf(); return ss.str();
    };
    CHECK(read(none_png) == read(def_png));

    remove_quietly(none_png);
    remove_quietly(def_png);
}

TEST_CASE("capture-screenshot --crop auto enlarges the content relative to the frame",
          "[integration][screenshot][crop]") {
    // On both a non-Tube and a Tube preset, --crop auto zooms the boot content
    // so its non-black bounding box fills more of the image than --crop none's.
    struct Case { std::string executable; std::string preset_id; };
    const std::vector<Case> cases = {
        {"beebium-model-b",          "model-b-disc"},
        {"beebium-model-b",          "model-b-disc-65c02-copro"},
    };

    auto unknown_mode = run_command(
        find_executable("beebium-model-b").string() +
        " capture-screenshot --output /tmp/beebium_crop_bad.png --crop sideways --duration 0");
    CHECK(unknown_mode.exit_code != 0);  // USAGE
    CHECK(unknown_mode.stderr_output.find("none") != std::string::npos);
    CHECK(unknown_mode.stderr_output.find("auto") != std::string::npos);

    for (const auto& c : cases) {
        auto exe = find_executable(c.executable);
        auto preset = exe.parent_path() / "presets" / (c.preset_id + ".preset.beebium");
        INFO("preset: " << c.preset_id);
        REQUIRE(std::filesystem::exists(preset));

        auto none_png = beebium::test::unique_temp_path("beebium_" + c.preset_id + "_none_", ".png");
        auto auto_png = beebium::test::unique_temp_path("beebium_" + c.preset_id + "_auto_", ".png");

        auto none = run_command(exe.string() + " capture-screenshot --preset \"" +
                                preset.string() + "\" --output \"" + none_png.string() +
                                "\" --crop none --duration 3");
        auto autor = run_command(exe.string() + " capture-screenshot --preset \"" +
                                 preset.string() + "\" --output \"" + auto_png.string() +
                                 "\" --crop auto --duration 3");
        REQUIRE(none.exit_code == 0);
        REQUIRE(autor.exit_code == 0);

        const double f_none = content_fraction(none_png);
        const double f_auto = content_fraction(auto_png);
        INFO("content fraction none=" << f_none << " auto=" << f_auto);
        REQUIRE(f_none > 0.0);
        REQUIRE(f_auto > 0.0);
        CHECK(f_auto > f_none);

        remove_quietly(none_png);
        remove_quietly(auto_png);
    }
}

// ============================================================================
// describe-rom parses sideways ROM headers
// ============================================================================

TEST_CASE("describe-rom parses a service ROM header", "[integration][describe-rom]") {
    auto result = run_command(EXECUTABLE + " describe-rom acorn-dfs_2_26.rom");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"recognised\": true") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"title\": \"DFS\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"version\": \"2.26\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"is_service_only\": true") != std::string::npos);
}

TEST_CASE("describe-rom reports a language ROM", "[integration][describe-rom]") {
    auto result = run_command(EXECUTABLE + " describe-rom bbc-basic_2.rom");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"title\": \"BASIC\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"is_language\": true") != std::string::npos);
}

TEST_CASE("describe-rom reports a non-sideways ROM as not recognised",
          "[integration][describe-rom]") {
    auto result = run_command(EXECUTABLE + " describe-rom acorn-mos_1_20.rom");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"recognised\": false") != std::string::npos);
}

TEST_CASE("describe-rom on a missing file returns NOINPUT", "[integration][describe-rom]") {
    auto result = run_command(EXECUTABLE + " describe-rom no-such-rom-12345.rom");
    REQUIRE(result.exit_code == 66);  // EX_NOINPUT
}

// ============================================================================
// describe-rom reports ROMFS-bearing ROMs
// ============================================================================

#ifdef BEEBIUM_TEST_ASSETS_DIR
TEST_CASE("describe-rom reports an Acornsoft cartridge as language + service + romfs",
          "[integration][describe-rom][romfs]") {
    auto rom_path = std::string(BEEBIUM_TEST_ASSETS_DIR) + "/roms/romfs/Electron_Hopper.rom";
    auto result = run_command(EXECUTABLE + " describe-rom \"" + rom_path + "\"");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"contains_romfs\": true") != std::string::npos);
    // Hopper is an auto-start cartridge (&C2): all three kinds.
    REQUIRE(result.stdout_output.find("\"language\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"service\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"romfs\"") != std::string::npos);
}

TEST_CASE("describe-rom reports a non-ROMFS service ROM with kinds=[service]",
          "[integration][describe-rom][romfs]") {
    auto result = run_command(EXECUTABLE + " describe-rom acorn-dfs_2_26.rom");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"contains_romfs\": false") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"service\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"romfs\"") == std::string::npos);
    REQUIRE(result.stdout_output.find("\"language\"") == std::string::npos);
}
#endif

// ============================================================================
// describe-disc-image validates a floppy disc image by the same rule the
// server applies on insert (the shared DiscFormatRegistry), so a front end
// configuring a machine before launch judges an image exactly as the running
// machine will.
// ============================================================================

#ifdef BEEBIUM_DISCS_DIR
namespace {
std::string disc_path(const std::string& name) {
    return (std::filesystem::path(BEEBIUM_DISCS_DIR) / name).string();
}
}  // namespace

TEST_CASE("describe-disc-image recognises an SSD image",
          "[integration][disc][describe-disc-image]") {
    const std::string path = disc_path("01-basic-validation.ssd");
    if (!std::filesystem::exists(path)) {
        SKIP("test disc image not available: " + path);
    }
    auto result = run_command(EXECUTABLE + " describe-disc-image \"" + path + "\"");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"recognised\": true") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"format\": \"SSD\"") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"sides\": 1") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"write_protected\":") != std::string::npos);
}

TEST_CASE("describe-disc-image recognises an HFE image",
          "[integration][disc][describe-disc-image]") {
    // A committed HFE fixture under tests/assets, not a game image beneath
    // the gitignored discs/games -- so it is present on every checkout,
    // including a clean CI runner, rather than only where one happens to have
    // been dropped by hand. No skip guard: a missing committed fixture should
    // fail loudly, as the describe-rom ROMFS tests treat theirs.
    const std::string path =
        std::string(BEEBIUM_TEST_ASSETS_DIR) + "/discs/Citadel_FSD0174_1.hfe";
    auto result = run_command(EXECUTABLE + " describe-disc-image \"" + path + "\"");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"recognised\": true") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"format\": \"HFE") != std::string::npos);
}

TEST_CASE("describe-disc-image reports a non-disc file as not recognised, with a reason",
          "[integration][disc][describe-disc-image]") {
    // A file that is plainly not a disc image. Exit is still 0 -- the file
    // was readable; the verdict is in the JSON, not the exit code.
    auto tmp = beebium::test::unique_temp_path("beebium_not_a_disc_", ".bin");
    {
        std::ofstream f(tmp, std::ios::binary);
        f << "this is not a disc image";
    }
    auto result = run_command(EXECUTABLE + " describe-disc-image \"" + tmp.string() + "\"");
    remove_quietly(tmp);

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"recognised\": false") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"reason\":") != std::string::npos);
}

TEST_CASE("describe-disc-image on a missing file returns NOINPUT",
          "[integration][disc][describe-disc-image]") {
    auto result = run_command(EXECUTABLE + " describe-disc-image /no/such/disc-98765.ssd");
    REQUIRE(result.exit_code == 66);  // EX_NOINPUT
    REQUIRE(result.stderr_output.find("cannot read") != std::string::npos);
}

TEST_CASE("describe-disc-image with no path returns USAGE",
          "[integration][disc][describe-disc-image]") {
    auto result = run_command(EXECUTABLE + " describe-disc-image");
    REQUIRE(result.exit_code == 64);  // EX_USAGE
}

TEST_CASE("describe-disc-image --help returns OK",
          "[integration][disc][describe-disc-image]") {
    auto result = run_command(EXECUTABLE + " describe-disc-image --help");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}
#endif  // BEEBIUM_DISCS_DIR

// ============================================================================
// AUN map file subcommands (share this harness; model-independent)
// ============================================================================

namespace {
// A unique temp aun-map.json path for a hermetic subcommand test.
std::filesystem::path temp_aun_map_filepath() {
    return beebium::test::unique_temp_path("beebium-aun-map-sub-", ".json");
}
}  // namespace

TEST_CASE("report-aun-map-filepath honours BEEBIUM_AUN_MAP_FILEPATH",
          "[integration][aun-map][report-aun-map-filepath]") {
    auto path = temp_aun_map_filepath();
    auto result = run_command(EXECUTABLE + " report-aun-map-filepath",
                              {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find(path.string()) != std::string::npos);
}

TEST_CASE("report-aun-map-filepath honours --map-file over the env",
          "[integration][aun-map][report-aun-map-filepath]") {
    auto path = temp_aun_map_filepath();
    auto result = run_command(
        EXECUTABLE + " report-aun-map-filepath --map-file " + path.string(),
        {{"BEEBIUM_AUN_MAP_FILEPATH", "/some/other/place.json"}});
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find(path.string()) != std::string::npos);
    REQUIRE(result.stdout_output.find("/some/other/place.json") ==
            std::string::npos);
}

TEST_CASE("create-aun-map writes a template and refuses to overwrite",
          "[integration][aun-map][create-aun-map]") {
    auto path = temp_aun_map_filepath();
    remove_quietly(path);
    auto created = run_command(EXECUTABLE + " create-aun-map",
                               {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(created.exit_code == 0);
    REQUIRE(std::filesystem::exists(path));

    auto again = run_command(EXECUTABLE + " create-aun-map",
                             {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(again.exit_code == 78);  // EX_CONFIG -- refuses to overwrite
    remove_quietly(path);
}

TEST_CASE("add-aun-peer then show-aun-map round-trips via the file",
          "[integration][aun-map][add-aun-peer]") {
    auto path = temp_aun_map_filepath();
    remove_quietly(path);
    auto added = run_command(
        EXECUTABLE + " add-aun-peer 0.254 192.168.1.50 32768 --label fs",
        {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(added.exit_code == 0);

    auto shown = run_command(EXECUTABLE + " --format tsv show-aun-map",
                             {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(shown.exit_code == 0);
    REQUIRE(shown.stdout_output.find("192.168.1.50") != std::string::npos);
    REQUIRE(shown.stdout_output.find("fs") != std::string::npos);
    remove_quietly(path);
}

TEST_CASE("add-aun-peer with a bad net.stn returns USAGE",
          "[integration][aun-map][add-aun-peer]") {
    auto path = temp_aun_map_filepath();
    auto result = run_command(
        EXECUTABLE + " add-aun-peer 0.999 192.168.1.50 32768",
        {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(result.exit_code == 64);  // EX_USAGE
    remove_quietly(path);
}

TEST_CASE("remove-aun-peer reports when nothing was removed",
          "[integration][aun-map][remove-aun-peer]") {
    auto path = temp_aun_map_filepath();
    remove_quietly(path);
    // Removing from an absent file is harmless and exits OK.
    auto result = run_command(EXECUTABLE + " remove-aun-peer 0.254",
                              {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("No entry") != std::string::npos);
    remove_quietly(path);
}

TEST_CASE("add-aun-subnet then remove-aun-subnet",
          "[integration][aun-map][add-aun-subnet]") {
    auto path = temp_aun_map_filepath();
    remove_quietly(path);
    auto added = run_command(EXECUTABLE + " add-aun-subnet 128 192.168.5.0/24",
                             {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(added.exit_code == 0);
    auto bad = run_command(EXECUTABLE + " add-aun-subnet 1 192.168.5.0/16",
                           {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(bad.exit_code == 64);  // not a /24
    auto removed = run_command(EXECUTABLE + " remove-aun-subnet 128",
                               {{"BEEBIUM_AUN_MAP_FILEPATH", path.string()}});
    REQUIRE(removed.exit_code == 0);
    REQUIRE(removed.stdout_output.find("Removed") != std::string::npos);
    remove_quietly(path);
}

// ============================================================================
// resolve-preset: the preset as `start` would load it, every key filled
// ============================================================================

namespace {

nlohmann::json resolve_json(const std::string& executable, const std::string& preset,
                            int expected_exit_code = 0,
                            const std::vector<std::pair<std::string, std::string>>& env = {}) {
    auto result = run_command(executable + " resolve-preset \"" + preset + "\" --json", env);
    INFO(preset << "\n" << result.stdout_output << result.stderr_output);
    REQUIRE(result.exit_code == expected_exit_code);
    return nlohmann::json::parse(result.stdout_output);
}

std::vector<std::filesystem::path> shipped_preset_filepaths() {
    auto presets_dirpath =
        std::filesystem::absolute(std::filesystem::path(EXECUTABLE)).parent_path() / "presets";
    std::vector<std::filesystem::path> filepaths;
    for (const auto& entry : std::filesystem::directory_iterator(presets_dirpath)) {
        const auto name = entry.path().filename().string();
        if (name.size() > 15 && name.substr(name.size() - 15) == ".preset.beebium") {
            filepaths.push_back(entry.path());
        }
    }
    std::sort(filepaths.begin(), filepaths.end());
    return filepaths;
}

std::string model_of(const std::filesystem::path& preset_filepath) {
    std::ifstream f(preset_filepath);
    return nlohmann::json::parse(f).at("model").get<std::string>();
}

}  // namespace

TEST_CASE("resolve-preset: every key of a minimal preset is filled",
          "[integration][preset][resolve-preset]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "minimal.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "name": "Minimal"})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string());

    CHECK(out.at("diagnostics").empty());
    const auto& p = out.at("preset");
    CHECK(p.at("model") == "model-b");
    CHECK(p.at("name") == "Minimal");
    CHECK(p.at("description").is_null());
    CHECK(p.at("release_date").is_null());
    CHECK(p.at("machine_name").is_null());
    CHECK(p.at("speed") == 1.0);
    CHECK(p.at("startup") == nlohmann::json{{"screen_mode", 7}, {"auto_boot", false}});
    CHECK(p.at("mos_image_uri") == "acorn-mos_1_20.rom");
    CHECK(p.at("storage").at("fdc_socket").at("id") == "none");
    REQUIRE(p.at("storage").at("floppy_drives").size() == 2);
    CHECK(p.at("storage").at("floppy_drives")[1] ==
          nlohmann::json{{"drive", 1}, {"image_uri", nullptr}});
    // The model's language ROM, as an explicit slot.
    CHECK(p.at("sideways_bank").at("slots") ==
          nlohmann::json::array({{{"slot", 15}, {"type", "rom"},
                                  {"image_uri", "bbc-basic_2.rom"}}}));
    CHECK(p.at("econet").is_null());
    CHECK(p.at("extensions").empty());
}

TEST_CASE("resolve-preset: extensions carry every parameter, typed, and their attachment",
          "[integration][preset][resolve-preset]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "ext.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b",
        "econet": {"station": "auto", "transport": {"name": "aun", "parameters": {"port": 0}}},
        "extensions": [{"name": "host-serial", "config": {"id": "modem", "baud": 9600}}]})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string());

    const auto& p = out.at("preset");
    CHECK(p.at("econet").at("station") == "auto");
    const auto& aun = p.at("econet").at("transport");
    CHECK(aun.at("name") == "aun");
    CHECK(aun.at("parameters").at("port") == "0");
    CHECK(aun.at("parameters").at("discovery") == "on");
    CHECK(aun.at("parameters").at("map") == nlohmann::json::array());
    REQUIRE(p.at("extensions").size() == 1);
    const auto& serial = p.at("extensions")[0];
    CHECK(serial.at("name") == "host-serial");
    CHECK(serial.at("config").at("id") == "modem");
    CHECK(serial.at("config").at("baud") == 9600);     // integer parameter: a number
    CHECK(serial.at("config").at("mode") == "pty");    // default
    CHECK(serial.at("attaches_to") == nlohmann::json::array({"serial-port"}));
}

TEST_CASE("resolve-preset: errors are reported with a JSON pointer and a non-zero exit",
          "[integration][preset][resolve-preset]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "bad.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "colour": "beige",
        "extensions": [{"name": "host-serial", "config": {"baud": "fast"}},
                       {"name": "host-serial", "config": {"wibble": 1}}]})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);

    std::vector<std::pair<std::string, std::string>> seen;
    for (const auto& d : out.at("diagnostics")) {
        seen.emplace_back(d.at("severity").get<std::string>(), d.at("path").get<std::string>());
    }
    using P = std::pair<std::string, std::string>;
    CHECK(std::find(seen.begin(), seen.end(), P{"warning", "/colour"}) != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), P{"error", "/extensions/0"}) != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), P{"error", "/extensions/1"}) != seen.end());
}

TEST_CASE("resolve-preset: an unreadable preset reports a null preset",
          "[integration][preset][resolve-preset]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "broken.preset.beebium";
    std::ofstream(preset_filepath) << "{ not json";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 66);

    CHECK(out.at("preset").is_null());
    REQUIRE(out.at("diagnostics").size() == 1);
    CHECK(out.at("diagnostics")[0].at("severity") == "error");
}

TEST_CASE("resolve-preset: a preset for another model is an error",
          "[integration][preset][resolve-preset]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "other.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b-plus"})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);

    CHECK(out.at("diagnostics")[0].at("path") == "/model");
}

// The save-as round trip: a resolved preset, saved through create-preset,
// resolves to itself. Run over every shipped preset.
TEST_CASE("resolve-preset: every shipped preset resolves and round-trips through create-preset",
          "[integration][preset][resolve-preset][create-preset]") {
    const auto filepaths = shipped_preset_filepaths();
    REQUIRE_FALSE(filepaths.empty());
    for (const auto& preset_filepath : filepaths) {
        INFO(preset_filepath.string());
        const auto executable = find_executable("beebium-" + model_of(preset_filepath)).string();

        auto first = resolve_json(executable, preset_filepath.string());
        for (const auto& d : first.at("diagnostics")) {
            CHECK(d.at("severity") != "error");
        }

        TempDirectory user_dir;
        std::ofstream(user_dir.path() / "resolved.preset.beebium") << first.at("preset").dump(2);
        auto saved_filepath = user_dir.path() / "saved.preset.beebium";
        const std::vector<std::pair<std::string, std::string>> env{
            {"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}};
        auto created = run_command(
            executable + " create-preset --from resolved --name \"" +
                first.at("preset").at("name").get<std::string>() + "\" --output \"" +
                saved_filepath.string() + "\"",
            env);
        REQUIRE(created.exit_code == 0);

        auto second = resolve_json(executable, saved_filepath.string());
        CHECK(second.at("preset") == first.at("preset"));

        // And the saved document builds the machine: every value it spells
        // out (an unset parameter as "", a default ROM as an explicit slot)
        // must mean what leaving it out means.
        auto png_filepath = user_dir.path() / "saved.png";
        auto built = run_command(
            executable + " capture-screenshot --preset \"" + saved_filepath.string() +
                "\" --duration 0 --output \"" + png_filepath.string() + "\"",
            {{"BEEBIUM_DISC_WORK_DIR", (user_dir.path() / "discs").string()}});
        INFO(built.stdout_output << built.stderr_output);
        CHECK(built.exit_code == 0);
    }
}

// ============================================================================
// describe-preset-schema: one section per preset key this model accepts
// ============================================================================

namespace {

nlohmann::json schema_section(const nlohmann::json& schema, const std::string& type) {
    for (const auto& section : schema.at("sections")) {
        if (section.at("type") == type) return section;
    }
    FAIL("no section of type " << type);
    return {};
}

nlohmann::json describe_schema(const std::string& executable) {
    auto result = run_command(executable + " describe-preset-schema");
    REQUIRE(result.exit_code == 0);
    return nlohmann::json::parse(result.stdout_output);
}

}  // namespace

TEST_CASE("describe-preset-schema: a section for every configurable preset key",
          "[integration][preset][describe-preset-schema]") {
    auto schema = describe_schema(EXECUTABLE);

    std::vector<std::string> types;
    for (const auto& section : schema.at("sections")) {
        types.push_back(section.at("type").get<std::string>());
    }
    for (const char* key : {"speed", "startup", "mos_image_uri", "storage", "sideways_bank",
                            "econet", "extensions"}) {
        CHECK(std::find(types.begin(), types.end(), key) != types.end());
    }
    CHECK(schema.at("model").at("id") == "model-b");
}

TEST_CASE("describe-preset-schema: startup gives both forms and their exclusion",
          "[integration][preset][describe-preset-schema]") {
    auto startup = schema_section(describe_schema(EXECUTABLE), "startup");
    CHECK(startup.at("screen_mode") == nlohmann::json{{"min", 0}, {"max", 7}, {"default", 7}});
    CHECK(startup.at("auto_boot").at("default") == false);
    CHECK(startup.at("links").at("max") == 255);
    CHECK(startup.at("links").at("excludes") ==
          nlohmann::json::array({"screen_mode", "auto_boot"}));
}

TEST_CASE("describe-preset-schema: the MOS default",
          "[integration][preset][describe-preset-schema]") {
    auto mos = schema_section(describe_schema(EXECUTABLE), "mos_image_uri");
    CHECK(mos.at("default") == "acorn-mos_1_20.rom");
}

TEST_CASE("describe-preset-schema: Econet stations and the transports to choose from",
          "[integration][preset][describe-preset-schema]") {
    auto econet = schema_section(describe_schema(EXECUTABLE), "econet");
    CHECK(econet.at("station").at("min") == 1);
    CHECK(econet.at("station").at("max") == 254);
    CHECK(econet.at("station").at("auto_default_range") == nlohmann::json::array({1, 253}));
    const auto& transports = econet.at("transports");
    CHECK(std::find(transports.begin(), transports.end(), "aun") != transports.end());
}

TEST_CASE("describe-preset-schema: the attachment points and what plugs into each",
          "[integration][preset][describe-preset-schema]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    std::map<std::string, nlohmann::json> points;
    for (const auto& p : extensions.at("attachment_points")) {
        points[p.at("id").get<std::string>()] = p;
    }
    REQUIRE(points.count("serial-port") == 1);
    CHECK(points["serial-port"].at("provided_by").is_null());
    const auto& serial = points["serial-port"].at("extensions");
    CHECK(std::find(serial.begin(), serial.end(), "host-serial") != serial.end());
    // A point an extension provides is listed with its provider.
    REQUIRE(points.count("scsi") == 1);
    CHECK(points["scsi"].at("provided_by") == nlohmann::json::array({"acorn-scsi"}));
    // Occupancy, from the same catalogue list-attachment-points prints.
    CHECK(points["serial-port"].at("display_name") == "Serial Port");
    CHECK(points["serial-port"].at("min_occupancy") == 0);
    CHECK(points["serial-port"].at("max_occupancy") == 1);
    CHECK(points["1mhz-bus"].at("max_occupancy").is_null());  // no fixed bound
    CHECK(points["scsi"].at("max_occupancy") == 7);            // per adapter
}

TEST_CASE("describe-preset-schema: an expansion board's own ROM is a default ROM",
          "[integration][preset][describe-preset-schema][sideways]") {
    auto sideways =
        schema_section(describe_schema(find_executable("beebium-model-b-integra-b").string()),
                       "sideways_bank");
    bool board = false;
    for (const auto& rom : sideways.at("default_roms")) {
        if (rom.at("role") == "board") board = true;
    }
    CHECK(board);
}

// ============================================================================
// Attachment checks before launch: SCSI discs and their adapters
// ============================================================================

namespace {

// The error diagnostics of resolving `body` as a Model B preset.
std::vector<std::pair<std::string, std::string>> attachment_errors(const std::string& body) {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "scsi.preset.beebium";
    std::ofstream(preset_filepath) << body;
    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);
    std::vector<std::pair<std::string, std::string>> errors;
    for (const auto& d : out.at("diagnostics")) {
        if (d.at("severity") == "error") {
            errors.emplace_back(d.at("path").get<std::string>(),
                                d.at("message").get<std::string>());
        }
    }
    return errors;
}

}  // namespace

TEST_CASE("resolve-preset: a SCSI disc with no adapter is an error at its entry",
          "[integration][preset][resolve-preset][attachment]") {
    auto errors = attachment_errors(R"({"model": "model-b", "extensions": [
        {"name": "scsi-hard-disc", "config": {"image": "/tmp/x.dat"}}]})");
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].first == "/extensions/0");
    CHECK(errors[0].second.find("acorn-scsi") != std::string::npos);
}

TEST_CASE("resolve-preset: two SCSI discs with one scsi-id is an error at the second",
          "[integration][preset][resolve-preset][attachment]") {
    auto errors = attachment_errors(R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"image": "/tmp/a.dat", "scsi-id": 1}},
        {"name": "scsi-hard-disc", "config": {"image": "/tmp/b.dat", "scsi-id": 1}}]})");
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].first == "/extensions/2/config/scsi-id");
}

TEST_CASE("resolve-preset: an adapter-id naming no adapter is an error at that parameter",
          "[integration][preset][resolve-preset][attachment]") {
    auto errors = attachment_errors(R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi", "config": {"id": "bus0"}},
        {"name": "scsi-hard-disc", "config": {"image": "/tmp/a.dat", "adapter-id": "bus9"}}]})");
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].first == "/extensions/1/config/adapter-id");
}

TEST_CASE("start: a SCSI disc with no adapter stops the launch with CONFIG",
          "[integration][preset][attachment]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "scsi.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "scsi-hard-disc", "config": {"image": "/tmp/x.dat"}}]})";
    auto result = run_command(EXECUTABLE + " start --preset \"" + preset_filepath.string() +
                              "\" --port 0");
    CHECK(result.exit_code == 78);
    CHECK(result.stderr_output.find("acorn-scsi") != std::string::npos);
}

// ============================================================================
// The bundled hard-disc masters, offered before launch
// ============================================================================

TEST_CASE("describe-preset-schema: the bundled hard-disc masters",
          "[integration][preset][describe-preset-schema][bundled]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    const auto& hard_discs = extensions.at("bundled_images").at("hard-disc");
    bool l3fs = false;
    for (const auto& image : hard_discs) {
        if (image.at("image") == "l3fs-v1_26b.dat") {
            l3fs = true;
            CHECK(image.at("size_bytes").get<std::uint64_t>() > 0);
        }
    }
    CHECK(l3fs);
}

TEST_CASE("describe-extension: a parameter that takes a bundled image says which kind",
          "[integration][extension][describe-extension][bundled]") {
    auto result = run_command(EXECUTABLE + " --format jsonl describe-extension scsi-hard-disc");
    REQUIRE(result.exit_code == 0);
    std::istringstream lines(result.stdout_output);
    std::string line;
    bool image_seen = false;
    while (std::getline(lines, line)) {
        auto p = nlohmann::json::parse(line);
        if (p.at("key") == "image") {
            image_seen = true;
            CHECK(p.at("bundled") == "hard-disc");
        } else {
            CHECK(p.at("bundled") == "");
        }
    }
    CHECK(image_seen);
}

TEST_CASE("resolve-preset: a bare hard-disc name that is not bundled is a warning",
          "[integration][preset][resolve-preset][bundled]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "nope.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"image": "no-such-master.dat"}}]})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string());

    REQUIRE(out.at("diagnostics").size() == 1);
    CHECK(out.at("diagnostics")[0].at("severity") == "warning");
    CHECK(out.at("diagnostics")[0].at("path") == "/extensions/1/config/image");
}

TEST_CASE("resolve-preset: every extension instance is written with an id",
          "[integration][preset][resolve-preset][ids]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "two-adapters.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi"}, {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"image": "/tmp/a.dat", "adapter-id": "acorn-scsi"}}]})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string());

    const auto& extensions = out.at("preset").at("extensions");
    CHECK(extensions[0].at("config").at("id") == "acorn-scsi");
    CHECK(extensions[1].at("config").at("id") == "acorn-scsi-1");
    CHECK(extensions[2].at("config").at("id") == "scsi-hard-disc");
}

// ============================================================================
// A plugin's own ROMs, named by bare name
// ============================================================================

TEST_CASE("describe-preset-schema: the ROMs extensions ship, with their extension",
          "[integration][preset][describe-preset-schema][bundled]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    bool found = false;
    for (const auto& rom : extensions.at("bundled_images").at("rom")) {
        if (rom.at("image") == "acorn-tube-6502_1_10.rom") {
            found = true;
            CHECK(rom.at("extension") == "acorn-65c02-coprocessor");
            CHECK(rom.at("size_bytes") == 4096);
        }
    }
    CHECK(found);
}

TEST_CASE("resolve-preset: a bare ROM name the extension does not ship is an error",
          "[integration][preset][resolve-preset][bundled]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "rom.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-65c02-coprocessor", "config": {"rom": "acorn-tube-65c102_1_20.rom"}}]})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);

    REQUIRE(out.at("diagnostics").size() == 1);
    CHECK(out.at("diagnostics")[0].at("path") == "/extensions/0/config/rom");
}

TEST_CASE("a coprocessor's ROM named by bare name loads the one the plugin ships",
          "[integration][preset][bundled]") {
    TempDirectory temp_dir;
    auto png_filepath = temp_dir.path() / "rom.png";
    auto ok = run_command(EXECUTABLE + " capture-screenshot --duration 0 --output \"" +
                          png_filepath.string() +
                          "\" --tube-65c102 rom=acorn-tube-65c102_1_20.rom");
    INFO(ok.stdout_output << ok.stderr_output);
    CHECK(ok.exit_code == 0);
    auto bad = run_command(EXECUTABLE + " capture-screenshot --duration 0 --output \"" +
                           png_filepath.string() + "\" --tube-65c102 rom=no-such.rom");
    CHECK(bad.exit_code == 78);
}

// ============================================================================
// resolve-preset: every error the preset's content causes is located
// ============================================================================

namespace {

// The single error resolving `body` gives, as (exit code, path).
std::pair<int, std::string> located_error(const std::string& body) {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "located.preset.beebium";
    std::ofstream(preset_filepath) << body;
    auto result = run_command(EXECUTABLE + " resolve-preset \"" + preset_filepath.string() +
                              "\" --json");
    auto out = nlohmann::json::parse(result.stdout_output);
    std::string path = "<none>";
    for (const auto& d : out.at("diagnostics")) {
        if (d.at("severity") == "error") path = d.at("path").get<std::string>();
    }
    return {result.exit_code, path};
}

}  // namespace

TEST_CASE("resolve-preset: Econet station errors are located at the station",
          "[integration][preset][resolve-preset][pointers]") {
    CHECK(located_error(R"({"econet": {"station": 255}})") ==
          std::make_pair(78, std::string("/econet/station")));
    CHECK(located_error(R"({"econet": {"transport": {"name": "aun"}}})") ==
          std::make_pair(78, std::string("/econet/station")));
    CHECK(located_error(R"({"econet": {"station": 80, "transport": {"parameters": {}}}})") ==
          std::make_pair(78, std::string("/econet/transport/name")));
}

TEST_CASE("resolve-preset: machine-setting errors are located at their key",
          "[integration][preset][resolve-preset][pointers]") {
    CHECK(located_error(R"({"speed": 0})") == std::make_pair(78, std::string("/speed")));
    CHECK(located_error(R"({"startup": {"screen_mode": 9}})") ==
          std::make_pair(78, std::string("/startup/screen_mode")));
    CHECK(located_error(R"({"startup": {"links": 3, "auto_boot": true}})") ==
          std::make_pair(78, std::string("/startup/links")));
}

TEST_CASE("resolve-preset: sideways errors are located at the slot",
          "[integration][preset][resolve-preset][pointers]") {
    CHECK(located_error(R"({"sideways_bank": {"slots": [
              {"slot": 15, "type": "rom", "image_uri": "x.rom"},
              {"slot": 16, "type": "empty"}]}})") ==
          std::make_pair(78, std::string("/sideways_bank/slots/1/slot")));
    CHECK(located_error(R"({"sideways_bank": {"slots": [{"slot": 14, "type": "rom"}]}})") ==
          std::make_pair(78, std::string("/sideways_bank/slots/0/image_uri")));
    // Slots found to conflict on the model's sockets, after loading, are
    // located at the later request.
    CHECK(located_error(R"({"sideways_bank": {"slots": [
              {"slot": 14, "type": "rom", "image_uri": "a.rom"},
              {"slot": 2, "type": "rom", "image_uri": "b.rom"}]}})") ==
          std::make_pair(78, std::string("/sideways_bank/slots/1")));
    // A write-protect request on a model with no write-protect control at
    // all is about the bank, not one slot.
    CHECK(located_error(R"({"sideways_bank": {"slots": [
              {"slot": 15, "type": "rom", "image_uri": "a.rom"},
              {"slot": 13, "type": "ram", "write_protected": true}]}})") ==
          std::make_pair(78, std::string("/sideways_bank")));
}

TEST_CASE("start: a preset whose content is invalid is a configuration error",
          "[integration][preset][pointers]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "bad-station.preset.beebium";
    std::ofstream(preset_filepath) << R"({"econet": {"station": 255}})";
    auto result = run_command(EXECUTABLE + " start --preset \"" + preset_filepath.string() +
                              "\" --port 0");
    CHECK(result.exit_code == 78);
}

// ============================================================================
// create-preset records what was given, parsed as start parses it
// ============================================================================

namespace {

nlohmann::json read_json(const std::filesystem::path& filepath) {
    std::ifstream f(filepath);
    return nlohmann::json::parse(f);
}

}  // namespace

TEST_CASE("create-preset: an extension records only the parameters given",
          "[integration][preset][create-preset][given]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "given.preset.beebium";
    auto result = run_command(EXECUTABLE + " create-preset --name \"Given\" --station 80 "
                              "--aun port=0 --host-serial baud=9600 --output \"" +
                              output_filepath.string() + "\"");
    REQUIRE(result.exit_code == 0);

    auto preset = read_json(output_filepath);
    CHECK(preset.at("econet").at("transport").at("parameters") ==
          nlohmann::json{{"port", "0"}});
    REQUIRE(preset.at("extensions").size() == 1);
    CHECK(preset.at("extensions")[0].at("config") == nlohmann::json{{"baud", 9600}});
}

TEST_CASE("create-preset: a launch-only option cannot be recorded",
          "[integration][preset][create-preset][given]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "port.preset.beebium";
    auto result = run_command(EXECUTABLE + " create-preset --name \"Port\" --port 4000 "
                              "--output \"" + output_filepath.string() + "\"");
    CHECK(result.exit_code == 64);
    CHECK(result.stderr_output.find("--port") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output_filepath));
}

TEST_CASE("create-preset --from: a flag overrides the source preset as start would",
          "[integration][preset][create-preset][given]") {
    TempDirectory user_dir;
    std::filesystem::create_directory(user_dir.path() / "discs");
    {
        std::ofstream f(user_dir.path() / "source.preset.beebium");
        f << R"({"model": "model-b", "name": "Source", "description": "From here",
                 "storage": {"floppy_drives": [{"drive": 0, "image_uri": "discs/game.ssd"}]},
                 "extensions": [{"name": "host-serial",
                                 "config": {"id": "modem", "mode": "device",
                                            "path": "/dev/ttyUSB0", "baud": 9600}}]})";
    }
    auto output_filepath = user_dir.path() / "copy.preset.beebium";
    auto result = run_command(
        EXECUTABLE + " create-preset --from source --name \"Copy\" "
        "--host-serial id=modem:baud=2400 --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_PRESETS_DIRPATH", user_dir.path().string()}});
    INFO(result.stderr_output);
    REQUIRE(result.exit_code == 0);

    auto preset = read_json(output_filepath);
    CHECK(preset.at("name") == "Copy");
    CHECK(preset.at("description") == "From here");
    REQUIRE(preset.at("extensions").size() == 1);
    CHECK(preset.at("extensions")[0].at("config") ==
          nlohmann::json{{"baud", 2400}, {"id", "modem"}, {"mode", "device"},
                         {"path", "/dev/ttyUSB0"}});
    // The source's relative floppy meant its own directory; the copy says so.
    CHECK(preset.at("storage").at("floppy_drives")[0].at("image_uri") ==
          "file://" + (std::filesystem::absolute(user_dir.path()) / "discs" / "game.ssd")
                          .lexically_normal().generic_string());
}

// ============================================================================
// resolve-preset's JSON output, pinned byte for byte: front-ends parse it
// ============================================================================

TEST_CASE("resolve-preset --json output is stable, diagnostics included",
          "[integration][preset][resolve-preset][pinned]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "pinned.preset.beebium";
    std::ofstream(preset_filepath)
        << R"({"model": "model-b", "name": "Pinned", "colour": "beige", "auto_boot": true,
 "extensions": [{"name": "host-serial", "config": {"baud": "fast"}}]})";

    auto result = run_command(EXECUTABLE + " resolve-preset \"" + preset_filepath.string() +
                              "\" --json");

    CHECK(result.exit_code == 78);
    CHECK(result.stdout_output == R"PINNED({
  "preset": {
    "model": "model-b",
    "name": "Pinned",
    "release_date": null,
    "description": null,
    "machine_name": null,
    "startup": {
      "screen_mode": 7,
      "auto_boot": true
    },
    "speed": 1.0,
    "mos_image_uri": "acorn-mos_1_20.rom",
    "storage": {
      "fdc_socket": {
        "id": "none"
      },
      "floppy_drives": [
        {
          "drive": 0,
          "image_uri": null
        },
        {
          "drive": 1,
          "image_uri": null
        }
      ]
    },
    "sideways_bank": {
      "slots": [
        {
          "slot": 15,
          "type": "rom",
          "image_uri": "bbc-basic_2.rom"
        }
      ]
    },
    "econet": null,
    "extensions": [
      {
        "name": "host-serial",
        "config": {
          "baud": "fast",
          "mode": "",
          "path": "",
          "tx_buffer": ""
        },
        "attaches_to": [
          "serial-port"
        ]
      }
    ],
    "thumbnail_capture_delay_seconds": null
  },
  "diagnostics": [
    {
      "severity": "warning",
      "path": "/colour",
      "message": "unknown key 'colour' is ignored"
    },
    {
      "severity": "warning",
      "path": "/auto_boot",
      "message": "a top-level 'auto_boot' is read as 'startup.auto_boot'; write it there instead"
    },
    {
      "severity": "error",
      "path": "/extensions/0",
      "message": "preset extension 'host-serial': parameter 'baud' must be an integer, got 'fast'"
    }
  ]
}
)PINNED");
}

TEST_CASE("resolve-preset --json output for an unloadable preset is stable",
          "[integration][preset][resolve-preset][pinned]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "bad.preset.beebium";
    std::ofstream(preset_filepath) << R"({"econet": {"station": 300}})";

    auto result = run_command(EXECUTABLE + " resolve-preset \"" + preset_filepath.string() +
                              "\" --json");

    CHECK(result.exit_code == 78);
    nlohmann::ordered_json expected{
        {"preset", nullptr},
        {"diagnostics",
         nlohmann::ordered_json::array(
             {{{"severity", "error"},
               {"path", "/econet/station"},
               {"message", "Econet station number must be between 1 and 254, got 300: " +
                               preset_filepath.string()}}})}};
    CHECK(result.stdout_output == expected.dump(2) + "\n");
}

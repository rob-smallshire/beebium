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
#include <catch2/generators/catch_generators.hpp>
#include <beebium/KeyboardMatrix.hpp>
#include <beebium/PlatformUtils.hpp>
#include <beebium/server/PresetLoader.hpp>
#include <beebium/zip/ZipArchive.hpp>
#include "stb_image.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>
#include "test_env.hpp"
#include "test_temp_paths.hpp"

namespace {

// Helper to run a command and capture stdout/stderr
struct ProcessResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

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

    // Set environment variables in current process - child processes inherit
    // them - restoring each afterwards (ctest pins some for every test).
    std::vector<std::unique_ptr<beebium::test::ScopedEnv>> scoped_env;
    for (const auto& [name, value] : env_vars) {
        scoped_env.push_back(std::make_unique<beebium::test::ScopedEnv>(name.c_str(), value));
    }

    // Build command with output redirection
    std::string full_command = command;
#ifdef _WIN32
    full_command += " >\"" + stdout_filepath.string() + "\" 2>\"" + stderr_filepath.string() + "\"";
#else
    full_command += " >" + stdout_filepath.string() + " 2>" + stderr_filepath.string();
#endif

    result.exit_code = std::system(full_command.c_str());

    scoped_env.clear();

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

    // Its presets/ subdirectory, made if absent: where a server whose
    // BEEBIUM_USER_DIRPATH is this directory keeps user presets.
    std::filesystem::path presets() const {
        const auto dirpath = path() / "presets";
        std::filesystem::create_directories(dirpath);
        return dirpath;
    }
};

const std::string EXECUTABLE = find_executable("beebium-model-b").string();

// Stores a preset whose document is `document` in the store `dirpath` as
// the archive <id>.beebium-preset, with `thumbnail` inside when given;
// returns the archive's path.
std::filesystem::path store_preset(const std::filesystem::path& dirpath, const std::string& id,
                                   const std::string& document,
                                   const std::optional<std::string>& thumbnail = std::nullopt) {
    std::vector<beebium::zip::ZipMember> members{
        {"preset.json", std::vector<std::uint8_t>(document.begin(), document.end())}};
    if (thumbnail) {
        members.push_back(
            {"thumbnail.png", std::vector<std::uint8_t>(thumbnail->begin(), thumbnail->end())});
    }
    const auto filepath = dirpath / (id + ".beebium-preset");
    beebium::zip::write_zip(filepath, std::move(members));
    return filepath;
}

// The document of the preset at `path`, in whichever form it is held.
std::string document_of(const std::filesystem::path& path) {
    std::string error;
    auto source = beebium::server::PresetSource::open(path, error);
    INFO(error);
    REQUIRE(source.has_value());
    return source->document_text();
}

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
    store_preset(user_dirpath, LONG_PRESET_ID, R"({"model": "model-b", "name": "Long Id Preset"})");
}

}  // namespace

TEST_CASE("list-presets: pretty id column leaves a gap after the longest id", "[integration][preset][list-presets]") {
    REQUIRE(LONG_PRESET_ID.size() == 30);
    TempDirectory temp_dir;
    write_long_id_user_preset(temp_dir.presets());

    auto result = run_command(EXECUTABLE + " --format pretty list-presets",
                              {{"BEEBIUM_USER_DIRPATH", temp_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    INFO(result.stdout_output);
    REQUIRE(result.stdout_output.find("  " + LONG_PRESET_ID + "  Long Id Preset\n") != std::string::npos);
}

TEST_CASE("list-presets: --format tsv yields tab-separated rows", "[integration][preset][list-presets]") {
    TempDirectory temp_dir;
    write_long_id_user_preset(temp_dir.presets());

    auto result = run_command(EXECUTABLE + " --format tsv list-presets",
                              {{"BEEBIUM_USER_DIRPATH", temp_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    INFO(result.stdout_output);
    REQUIRE(result.stdout_output.rfind("id\tname\tsource\n", 0) == 0);
    REQUIRE(result.stdout_output.find("\nmodel-b\t") != std::string::npos);
    REQUIRE(result.stdout_output.find("\n" + LONG_PRESET_ID + "\tLong Id Preset\tuser\n") != std::string::npos);
}

TEST_CASE("list-presets: --format jsonl and --json yield the same rows", "[integration][preset][list-presets]") {
    TempDirectory temp_dir;
    write_long_id_user_preset(temp_dir.presets());
    std::vector<std::pair<std::string, std::string>> env{
        {"BEEBIUM_USER_DIRPATH", temp_dir.path().string()}};

    auto jsonl = run_command(EXECUTABLE + " --format jsonl list-presets", env);
    auto json = run_command(EXECUTABLE + " list-presets --json", env);

    REQUIRE(jsonl.exit_code == 0);
    REQUIRE(json.exit_code == 0);
    REQUIRE(jsonl.stdout_output == json.stdout_output);
    INFO(jsonl.stdout_output);
    nlohmann::ordered_json expected{{"id", LONG_PRESET_ID},
                                    {"name", "Long Id Preset"},
                                    {"source", "user"},
                                    {"model", "model-b"},
                                    {"filepath", (temp_dir.presets() /
                                                  (LONG_PRESET_ID + ".beebium-preset")).string()},
                                    {"thumbnail", false}};
    REQUIRE(jsonl.stdout_output.find(expected.dump() + "\n") != std::string::npos);
}

namespace {

// The jsonl row list-presets gives for `id`, as printed, or "" when none.
std::string jsonl_row(const std::string& output, const std::string& id) {
    std::istringstream lines(output);
    for (std::string line; std::getline(lines, line);) {
        if (nlohmann::json::parse(line).at("id") == id) return line;
    }
    return "";
}

}  // namespace

TEST_CASE("list-presets jsonl: every field a preset gives, in a fixed order, and its thumbnail",
          "[integration][preset][list-presets]") {
    TempDirectory user_dir;
    const auto filepath = store_preset(user_dir.presets(), "full",
                                       R"({"release_date": "1984", "model": "model-b",
        "description": "Every field", "machine_name": "Full {station}", "name": "Full"})",
                                       "PNG");

    auto result = run_command(EXECUTABLE + " --format jsonl list-presets",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    nlohmann::ordered_json expected{{"id", "full"},
                                    {"name", "Full"},
                                    {"source", "user"},
                                    {"model", "model-b"},
                                    {"machine_name", "Full {station}"},
                                    {"description", "Every field"},
                                    {"release_date", "1984"},
                                    {"filepath", filepath.string()},
                                    {"thumbnail", true}};
    CHECK(jsonl_row(result.stdout_output, "full") == expected.dump());
}

TEST_CASE("list-presets jsonl: a field a preset does not give is omitted, and an unnamed preset "
          "is called by its model's name",
          "[integration][preset][list-presets]") {
    TempDirectory user_dir;
    const auto plus_filepath =
        store_preset(user_dir.presets(), "plus", R"({"model": "model-b-plus"})");
    const auto any_filepath =
        store_preset(user_dir.presets(), "any", R"({"description": "Suits any model"})");
    std::vector<std::pair<std::string, std::string>> env{
        {"BEEBIUM_USER_DIRPATH", user_dir.path().string()}};

    // This executable's model: the model-b-plus preset is not listed; the one
    // naming no model is, called by this model's name.
    auto own = run_command(EXECUTABLE + " --format jsonl list-presets", env);
    REQUIRE(own.exit_code == 0);
    CHECK(jsonl_row(own.stdout_output, "plus").empty());
    nlohmann::ordered_json any{{"id", "any"},
                               {"name", "BBC Model B"},
                               {"source", "user"},
                               {"description", "Suits any model"},
                               {"filepath", any_filepath.string()},
                               {"thumbnail", false}};
    CHECK(jsonl_row(own.stdout_output, "any") == any.dump());

    // Every model's: the model-b-plus preset is called by its own model's name.
    auto all = run_command(EXECUTABLE + " --format jsonl list-presets --all-models", env);
    REQUIRE(all.exit_code == 0);
    nlohmann::ordered_json plus{{"id", "plus"},
                                {"name", "BBC Model B+ 64K"},
                                {"source", "user"},
                                {"model", "model-b-plus"},
                                {"filepath", plus_filepath.string()},
                                {"thumbnail", false}};
    CHECK(jsonl_row(all.stdout_output, "plus") == plus.dump());
    CHECK(jsonl_row(all.stdout_output, "any") == any.dump());
}

TEST_CASE("list-presets --all-models lists every model's system presets from one executable",
          "[integration][preset][list-presets]") {
    TempDirectory user_dir;
    auto result = run_command(EXECUTABLE + " --format jsonl list-presets --all-models",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});
    REQUIRE(result.exit_code == 0);
    std::set<std::string> models;
    std::istringstream lines(result.stdout_output);
    for (std::string line; std::getline(lines, line);) {
        auto row = nlohmann::json::parse(line);
        CHECK(row.at("source") == "system");
        if (row.contains("model")) models.insert(row.at("model").get<std::string>());
    }
    CHECK(models.count("model-b") == 1);
    CHECK(models.count("model-b-plus") == 1);
    CHECK(models.count("model-b-integra-b") == 1);
}

TEST_CASE("list-presets --help: returns OK", "[integration][preset][list-presets]") {
    auto result = run_command(EXECUTABLE + " list-presets --help");

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stderr_output.find("Usage:") != std::string::npos);
}

// ============================================================================
// show-preset integration tests
// ============================================================================

TEST_CASE("show-preset: shows a preset named by id or by path", "[integration][preset][show-preset]") {
    TempDirectory user_dir;
    TempDirectory elsewhere;
    {
        store_preset(user_dir.presets(), "mine", R"({"model": "model-b", "name": "By Id"})");
        std::ofstream(elsewhere.path() / "loose.json")
            << R"({"model": "model-b", "name": "By Path"})";
    }
    std::vector<std::pair<std::string, std::string>> env{
        {"BEEBIUM_USER_DIRPATH", user_dir.path().string()}};

    auto by_id = run_command(EXECUTABLE + " show-preset mine", env);
    REQUIRE(by_id.exit_code == 0);
    CHECK(by_id.stdout_output.find("By Id") != std::string::npos);

    auto by_path = run_command(
        EXECUTABLE + " show-preset \"" + (elsewhere.path() / "loose.json").string() + "\"", env);
    REQUIRE(by_path.exit_code == 0);
    CHECK(by_path.stdout_output.find("By Path") != std::string::npos);
}

TEST_CASE("show-preset: a preset named by nothing says what was searched", "[integration][preset][show-preset]") {
    auto result = run_command(EXECUTABLE + " show-preset nonexistent-preset-12345");
    REQUIRE(result.exit_code == 66);
    CHECK(result.stderr_output.find("Preset not found: 'nonexistent-preset-12345' (no such file, "
                                    "and no system or user preset with that id)") !=
          std::string::npos);
}

TEST_CASE("show-preset --thumbnail: writes a stored or directory preset's thumbnail",
          "[integration][preset][show-preset]") {
    TempDirectory user_dir;
    TempDirectory out_dir;
    store_preset(user_dir.presets(), "pic", R"({"model": "model-b"})", "STORED-PNG");
    const auto dirpath = out_dir.path() / "dir-preset";
    std::filesystem::create_directories(dirpath);
    std::ofstream(dirpath / "preset.json") << R"({"model": "model-b-plus"})";
    std::ofstream(dirpath / "thumbnail.png", std::ios::binary) << "DIR-PNG";
    std::vector<std::pair<std::string, std::string>> env{
        {"BEEBIUM_USER_DIRPATH", user_dir.path().string()}};
    auto read = [](const std::filesystem::path& filepath) {
        std::ifstream f(filepath, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };

    const auto stored_png = out_dir.path() / "stored.png";
    auto stored = run_command(EXECUTABLE + " show-preset pic --thumbnail --output \"" +
                                  stored_png.string() + "\"", env);
    REQUIRE(stored.exit_code == 0);
    CHECK(read(stored_png) == "STORED-PNG");

    // Another model's preset, named by path: any executable shows it.
    const auto dir_png = out_dir.path() / "dir.png";
    auto dir = run_command(EXECUTABLE + " show-preset \"" + dirpath.string() +
                               "\" --thumbnail --output \"" + dir_png.string() + "\"", env);
    REQUIRE(dir.exit_code == 0);
    CHECK(read(dir_png) == "DIR-PNG");
}

TEST_CASE("show-preset --thumbnail: a preset without one writes nothing and says so",
          "[integration][preset][show-preset]") {
    TempDirectory user_dir;
    store_preset(user_dir.presets(), "bare", R"({"model": "model-b"})");
    const auto png = user_dir.path() / "out.png";

    auto result = run_command(EXECUTABLE + " show-preset bare --thumbnail --output \"" +
                                  png.string() + "\"",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 66);
    CHECK(result.stderr_output == "Error: preset bare has no thumbnail\n");
    CHECK_FALSE(std::filesystem::exists(png));
}

TEST_CASE("show-preset: --thumbnail and --output go together", "[integration][preset][show-preset]") {
    CHECK(run_command(EXECUTABLE + " show-preset model-b --thumbnail").exit_code == 64);
    CHECK(run_command(EXECUTABLE + " show-preset model-b --output x.png").exit_code == 64);
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

TEST_CASE("create-preset --output: writes the preset as an archive holding preset.json",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "test-preset.beebium-preset";

    auto result = run_command(EXECUTABLE + " create-preset --name \"Test Preset\" --output \"" +
                              output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 0);
    CHECK(beebium::zip::ZipReader(output_filepath).member_names() ==
          std::vector<std::string>{"preset.json"});
    const auto content = document_of(output_filepath);
    CHECK(content.find("\"model\": \"model-b\"") != std::string::npos);
    CHECK(content.find("\"name\": \"Test Preset\"") != std::string::npos);
}

TEST_CASE("create-preset --unpack: writes the preset as a directory, laid out as its archive",
          "[integration][preset][create-preset]") {
    TempDirectory temp_dir;
    const auto output_dirpath = temp_dir.path() / "unpacked";

    auto result = run_command(EXECUTABLE + " create-preset --name Unpacked --unpack --output \"" +
                              output_dirpath.string() + "\"");

    REQUIRE(result.exit_code == 0);
    CHECK(std::filesystem::is_regular_file(output_dirpath / "preset.json"));
    CHECK(document_of(output_dirpath).find("\"name\": \"Unpacked\"") != std::string::npos);
    // Not over an existing directory, and not without --output.
    CHECK(run_command(EXECUTABLE + " create-preset --name Again --unpack --output \"" +
                      output_dirpath.string() + "\"")
              .exit_code == 74);
    CHECK(run_command(EXECUTABLE + " create-preset --name Nowhere --unpack").exit_code == 64);
}

TEST_CASE("create-preset: creates preset in user directory", "[integration][preset][create-preset]") {
    TempDirectory temp_dir;

    auto result = run_command(
        EXECUTABLE + " create-preset --name \"My Test Setup\"",
        {{"BEEBIUM_USER_DIRPATH", temp_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    // Output should be the preset ID
    REQUIRE(result.stdout_output.find("my-test-setup") != std::string::npos);

    // Verify file was created
    auto preset_filepath = temp_dir.presets() / "my-test-setup.beebium-preset";
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
          beebium::server::normalize_image_uri(
              (std::filesystem::current_path() / "discs" / "game.ssd").string(), {}));
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
    auto preset_filepath = store_preset(temp_dir.presets(), "deleteme",
                                        R"({"model": "model-b", "name": "Delete Me"})");
    REQUIRE(std::filesystem::exists(preset_filepath));

    // Delete it
    auto result = run_command(
        EXECUTABLE + " delete-preset deleteme",
        {{"BEEBIUM_USER_DIRPATH", temp_dir.path().string()}}
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

TEST_CASE("import-preset: a directory is stored archived, with every file it holds",
          "[integration][preset][import-preset]") {
    TempDirectory user_dir;
    TempDirectory source_dir;
    const auto dirpath = source_dir.path() / "My Game";
    std::filesystem::create_directories(dirpath / "discs");
    std::ofstream(dirpath / "preset.json") << R"({"model": "model-b"})";
    std::ofstream(dirpath / "thumbnail.png", std::ios::binary) << "PNG";
    std::ofstream(dirpath / "discs" / "game.ssd", std::ios::binary) << "DISC";
    std::ofstream(dirpath / ".DS_Store") << "hidden";

    auto result = run_command(EXECUTABLE + " import-preset \"" + dirpath.string() + "\"",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    CHECK(result.stdout_output == "my-game\n");
    beebium::zip::ZipReader stored(user_dir.presets() / "my-game.beebium-preset");
    CHECK(stored.member_names() ==
          std::vector<std::string>{"discs/game.ssd", "preset.json", "thumbnail.png"});
    CHECK(stored.read("discs/game.ssd") == std::vector<std::uint8_t>{'D', 'I', 'S', 'C'});
}

TEST_CASE("import-preset: an archive is stored with every member it holds",
          "[integration][preset][import-preset]") {
    TempDirectory user_dir;
    TempDirectory source_dir;
    const std::string document = R"({"model": "model-b-plus"})";
    beebium::zip::write_zip(
        source_dir.path() / "zipped.zip",
        {{"preset.json", std::vector<std::uint8_t>(document.begin(), document.end())},
         {"roms/x.rom", {'R'}}});

    auto result = run_command(
        EXECUTABLE + " import-preset \"" + (source_dir.path() / "zipped.zip").string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    CHECK(result.stdout_output == "zipped\n");
    beebium::zip::ZipReader stored(user_dir.presets() / "zipped.beebium-preset");
    CHECK(stored.member_names() == std::vector<std::string>{"preset.json", "roms/x.rom"});
    CHECK(document_of(user_dir.presets() / "zipped.beebium-preset") == document);
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
        {{"BEEBIUM_USER_DIRPATH", dest_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("importme") != std::string::npos);

    // The preset is stored as an archive, its document exactly as written.
    auto imported_filepath = dest_dir.presets() / "importme.beebium-preset";
    CHECK(document_of(imported_filepath) == R"({"model": "model-b", "name": "Import Me"})");
}

TEST_CASE("import-preset: another model's preset is imported by any executable",
          "[integration][preset][import-preset]") {
    TempDirectory source_dir;
    TempDirectory user_dir;
    const auto source_filepath = source_dir.path() / "plus.preset.beebium";
    std::ofstream(source_filepath) << R"({"model": "model-b-plus", "name": "Plus"})";

    auto result = run_command(EXECUTABLE + " import-preset \"" + source_filepath.string() + "\"",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    CHECK(result.stdout_output == "plus\n");
    CHECK(std::filesystem::exists(user_dir.presets() / "plus.beebium-preset"));
}

TEST_CASE("import-preset: a preset naming an unknown model is refused",
          "[integration][preset][import-preset]") {
    TempDirectory source_dir;
    TempDirectory user_dir;
    const auto source_filepath = source_dir.path() / "wrong-model.preset.beebium";
    std::ofstream(source_filepath) << R"({"model": "wrong-model", "name": "Wrong Model"})";

    auto result = run_command(EXECUTABLE + " import-preset \"" + source_filepath.string() + "\"",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 65);  // DATAERR
    CHECK(result.stderr_output.find("preset model 'wrong-model' is not a known model (one of: "
                                    "model-b, model-b-plus, ") != std::string::npos);
    CHECK(std::filesystem::is_empty(user_dir.presets()));
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
    auto result = run_command(EXECUTABLE + " export-preset --output /tmp/out.beebium-preset");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("export-preset: missing --output returns USAGE (64)", "[integration][preset][export-preset]") {
    auto result = run_command(EXECUTABLE + " export-preset some-id");

    REQUIRE(result.exit_code == 64);
}

TEST_CASE("export-preset: nonexistent preset returns NOINPUT (66)", "[integration][preset][export-preset]") {
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "exported.beebium-preset";

    auto result = run_command(EXECUTABLE + " export-preset nonexistent-12345 --output \"" +
                              output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 66);
}

TEST_CASE("export-preset: exports user preset", "[integration][preset][export-preset]") {
    TempDirectory user_dir;
    TempDirectory output_dir;

    // Create source preset in user directory
    store_preset(user_dir.presets(), "mypreset", R"({"model": "model-b", "name": "My Preset"})",
                 "PNG");

    auto output_filepath = output_dir.path() / "exported.beebium-preset";

    auto result = run_command(
        EXECUTABLE + " export-preset mypreset --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}}
    );

    REQUIRE(result.exit_code == 0);
    // The archive, thumbnail and all.
    CHECK(beebium::zip::ZipReader(output_filepath).member_names() ==
          std::vector<std::string>{"preset.json", "thumbnail.png"});
    CHECK(document_of(output_filepath).find("\"name\": \"My Preset\"") != std::string::npos);
}

TEST_CASE("export-preset --unpack: writes a new directory laid out as the archive",
          "[integration][preset][export-preset]") {
    TempDirectory user_dir;
    const auto unpacked = user_dir.path() / "unpacked";
    store_preset(user_dir.presets(), "pic", R"({"model": "model-b"})", "PNG");
    std::vector<std::pair<std::string, std::string>> env{
        {"BEEBIUM_USER_DIRPATH", user_dir.path().string()}};

    auto result = run_command(
        EXECUTABLE + " export-preset pic --unpack --output \"" + unpacked.string() + "\"", env);

    REQUIRE(result.exit_code == 0);
    CHECK(document_of(unpacked) == R"({"model": "model-b"})");
    std::ifstream png(unpacked / "thumbnail.png", std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(png), {}) == "PNG");
    // Never over an existing directory.
    auto again = run_command(
        EXECUTABLE + " export-preset pic --unpack --output \"" + unpacked.string() + "\"", env);
    CHECK(again.exit_code == 74);
}

TEST_CASE("export-preset: exports a preset named by path", "[integration][preset][export-preset]") {
    TempDirectory source_dir;
    TempDirectory output_dir;
    const auto source_filepath = source_dir.path() / "loose.json";
    std::ofstream(source_filepath) << R"({"model": "model-b", "name": "Loose"})";
    const auto output_filepath = output_dir.path() / "exported.beebium-preset";

    auto result = run_command(EXECUTABLE + " export-preset \"" + source_filepath.string() +
                              "\" --output \"" + output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 0);
    CHECK(document_of(output_filepath) == R"({"model": "model-b", "name": "Loose"})");
}

TEST_CASE("export-preset: an output that cannot be written is an IO error",
          "[integration][preset][export-preset]") {
    TempDirectory source_dir;
    const auto source_filepath = source_dir.path() / "loose.json";
    std::ofstream(source_filepath) << R"({"model": "model-b"})";
    const auto output_filepath = source_dir.path() / "no-such-dir" / "out.beebium-preset";

    auto result = run_command(EXECUTABLE + " export-preset \"" + source_filepath.string() +
                              "\" --output \"" + output_filepath.string() + "\"");

    REQUIRE(result.exit_code == 74);  // IOERR
    CHECK(result.stderr_output.find("cannot write preset: " + output_filepath.string()) !=
          std::string::npos);
}

TEST_CASE("export-preset and create-preset --from refuse to write over the preset they read",
          "[integration][preset][export-preset][create-preset]") {
    TempDirectory temp_dir;
    const auto archive_filepath =
        store_preset(temp_dir.path(), "same", R"({"model": "model-b", "name": "Same"})", "PNG");
    std::ifstream before_file(archive_filepath, std::ios::binary);
    const std::string before((std::istreambuf_iterator<char>(before_file)), {});
    before_file.close();
    // The same file, spelt another way.
    const auto respelt = temp_dir.path() / "." / "same.beebium-preset";

    auto exported = run_command(EXECUTABLE + " export-preset \"" + archive_filepath.string() +
                                "\" --output \"" + respelt.string() + "\"");
    CHECK(exported.exit_code == 64);
    CHECK(exported.stderr_output.find("--output names the preset being read") !=
          std::string::npos);

    auto created = run_command(EXECUTABLE + " create-preset --name Copy --from \"" +
                               archive_filepath.string() + "\" --output \"" +
                               archive_filepath.string() + "\"");
    CHECK(created.exit_code == 64);
    CHECK(created.stderr_output.find("--output names the preset being read") !=
          std::string::npos);

    // A directory preset unpacked over itself, likewise.
    const auto dirpath = temp_dir.path() / "dir-preset";
    std::filesystem::create_directories(dirpath);
    std::ofstream(dirpath / "preset.json") << R"({"model": "model-b"})";
    auto unpacked = run_command(EXECUTABLE + " export-preset \"" + dirpath.string() +
                                "\" --unpack --output \"" + dirpath.string() + "\"");
    CHECK(unpacked.exit_code == 64);

    std::ifstream after_file(archive_filepath, std::ios::binary);
    CHECK(std::string((std::istreambuf_iterator<char>(after_file)), {}) == before);
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
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(create_result.exit_code == 0);

    std::string preset_id = create_result.stdout_output;
    // Trim whitespace
    while (!preset_id.empty() && (preset_id.back() == '\n' || preset_id.back() == '\r')) {
        preset_id.pop_back();
    }
    REQUIRE_FALSE(preset_id.empty());

    // Verify preset file exists
    auto preset_filepath = user_dir.presets() / (preset_id + ".beebium-preset");
    REQUIRE(std::filesystem::exists(preset_filepath));

    // Step 2: Export the preset
    auto export_filepath = export_dir.path() / "exported.beebium-preset";
    auto export_result = run_command(
        EXECUTABLE + " export-preset " + preset_id + " --output \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(export_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(export_filepath));

    // Step 3: Delete the original
    auto delete_result = run_command(
        EXECUTABLE + " delete-preset " + preset_id,
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(delete_result.exit_code == 0);
    REQUIRE_FALSE(std::filesystem::exists(preset_filepath));

    // Step 4: Import from export
    auto import_result = run_command(
        EXECUTABLE + " import-preset \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", import_dir.path().string()}}
    );
    REQUIRE(import_result.exit_code == 0);

    // Verify imported file exists
    auto imported_filepath = import_dir.presets() / "exported.beebium-preset";
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
    store_preset(user_dir.presets(), "sideways-src",
                 R"({"name": "Sideways Source", "model": "model-b", )"
                 R"("sideways_bank": { "slots": [ )"
                 R"({ "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" } ] }})");

    // Copy via create-preset --from: the section should survive the copy.
    auto create_result = run_command(
        EXECUTABLE + " create-preset --from sideways-src --name \"Sideways Copy\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(create_result.exit_code == 0);

    std::string new_id = create_result.stdout_output;
    while (!new_id.empty() &&
           (new_id.back() == '\n' || new_id.back() == '\r' || new_id.back() == ' ')) {
        new_id.pop_back();
    }
    REQUIRE_FALSE(new_id.empty());

    auto copy_filepath = user_dir.presets() / (new_id + ".beebium-preset");
    REQUIRE(std::filesystem::exists(copy_filepath));
    std::string copy_contents = document_of(copy_filepath);
    REQUIRE(copy_contents.find("sideways_bank") != std::string::npos);
    REQUIRE(copy_contents.find("acorn-dfs_2_26.rom") != std::string::npos);

    // Export the copy: the section should survive export too.
    auto export_filepath = user_dir.path() / "exported-sideways.beebium-preset";
    auto export_result = run_command(
        EXECUTABLE + " export-preset " + new_id + " --output \"" + export_filepath.string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}}
    );
    REQUIRE(export_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(export_filepath));
    std::string export_contents = document_of(export_filepath);
    REQUIRE(export_contents.find("sideways_bank") != std::string::npos);
    REQUIRE(export_contents.find("acorn-dfs_2_26.rom") != std::string::npos);
}

TEST_CASE("create-preset --from: --sideways replaces only the slots it names",
          "[integration][preset][create-preset][sideways]") {
    TempDirectory user_dir;
    store_preset(user_dir.presets(), "slots-src",
                 R"({"name": "Slots Source", "model": "model-b", "auto_boot": true, )"
                 R"("sideways_bank": { "slots": [ )"
                 R"({ "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" }, )"
                 R"({ "slot": 13, "type": "empty" } ] }})");
    auto output_filepath = user_dir.path() / "slots-copy.beebium-preset";
    auto result = run_command(
        EXECUTABLE + " create-preset --from slots-src --name \"Slots Copy\" "
        "--sideways slot=13:type=ram --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});
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
    auto output_filepath = beebium::test::unique_temp_path("beebium_gen_disc_", ".beebium-preset");

    auto create_result = run_command(
        EXECUTABLE + " create-preset --name \"Gen Disc\" --release-date 1982"
        " --fdc acorn-1770 --sideways slot=14:type=rom:image=acorn-dfs_2_26.rom"
        " --output \"" + output_filepath.string() + "\"");
    REQUIRE(create_result.exit_code == 0);
    REQUIRE(std::filesystem::exists(output_filepath));

    std::string contents = document_of(output_filepath);
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
            executable.parent_path() / "presets" / (c.preset_id + ".beebium-preset");
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
        auto tube_preset = presets_dir / (c.tube_preset + ".beebium-preset");
        auto host_preset = presets_dir / (c.host_preset + ".beebium-preset");
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
    auto preset = exe.parent_path() / "presets" / "model-b-disc.beebium-preset";
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
        auto preset = exe.parent_path() / "presets" / (c.preset_id + ".beebium-preset");
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
        if (name.size() > 15 && name.substr(name.size() - 15) == ".beebium-preset") {
            filepaths.push_back(entry.path());
        }
    }
    std::sort(filepaths.begin(), filepaths.end());
    return filepaths;
}

std::string model_of(const std::filesystem::path& preset_filepath) {
    return nlohmann::json::parse(document_of(preset_filepath)).at("model").get<std::string>();
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

TEST_CASE("resolve-preset: a directory preset resolves its references in the directory",
          "[integration][preset][resolve-preset][preset_source]") {
    TempDirectory temp_dir;
    const auto dirpath = temp_dir.path() / "my-game";
    std::filesystem::create_directories(dirpath / "discs");
    std::ofstream(dirpath / "discs" / "game.ssd") << std::string(2560, '\0');
    std::ofstream(dirpath / "preset.json") << R"({"model": "model-b",
        "storage": {"fdc_socket": {"id": "acorn-1770"},
                    "floppy_drives": [{"drive": 0, "image_uri": "discs/game.ssd"}]}})";

    auto out = resolve_json(EXECUTABLE, dirpath.string());

    CHECK(out.at("diagnostics").empty());
    const auto& p = out.at("preset");
    CHECK(p.at("name") == "BBC Model B");  // unnamed: its model's display name
    CHECK(p.at("storage").at("floppy_drives")[0].at("image_uri") ==
          beebium::file_url_from_filepath(dirpath / "discs" / "game.ssd"));
}

TEST_CASE("resolve-preset: a floppy named with a space or a percent sign opens that file",
          "[integration][preset][resolve-preset]") {
    // The image_uri resolve-preset reports is the one `start` opens, through
    // DiscUrl, which decodes: it must be encoded, or "a%41.ssd" opens "aA.ssd".
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "floppies.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "name": "Floppies",
        "storage": {"fdc_socket": {"id": "acorn-1770"}, "floppy_drives": [
            {"drive": 0, "image_uri": "./a%41.ssd"},
            {"drive": 1, "image_uri": "./My Disc.ssd"}]}})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string());

    const auto& drives = out.at("preset").at("storage").at("floppy_drives");
    REQUIRE(drives.size() == 2);
    const std::pair<int, const char*> expected[] = {{0, "a%41.ssd"}, {1, "My Disc.ssd"}};
    for (const auto& [drive, name] : expected) {
        const auto uri = drives[drive].at("image_uri").get<std::string>();
        INFO(uri);
        const auto url = beebium::DiscUrl::parse(uri);
        REQUIRE(url.has_value());
        CHECK(url->to_filepath() ==
              (std::filesystem::absolute(temp_dir.path()) / name).lexically_normal());
    }
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

namespace {

// Each diagnostic's location, by its path.
std::map<std::string, std::string> locations_by_path(const nlohmann::json& out) {
    std::map<std::string, std::string> locations;
    for (const auto& d : out.at("diagnostics")) {
        locations[d.at("path").get<std::string>()] = d.at("location").get<std::string>();
    }
    return locations;
}

}  // namespace

TEST_CASE("resolve-preset: a diagnostic says where it is in the words the schema uses",
          "[integration][preset][resolve-preset]") {
    TempDirectory temp_dir;

    SECTION("an extension by its display name, an unknown key by itself") {
        auto preset_filepath = temp_dir.path() / "bad.preset.beebium";
        std::ofstream(preset_filepath) << R"({"model": "model-b", "colour": "beige",
            "extensions": [{"name": "host-serial", "config": {"baud": "fast"}}]})";
        const auto locations =
            locations_by_path(resolve_json(EXECUTABLE, preset_filepath.string(), 78));
        INFO(nlohmann::json(locations).dump());
        CHECK(locations.at("/colour") == "colour");
        bool extension_located = false;
        for (const auto& [path, location] : locations) {
            if (path.starts_with("/extensions/0")) {
                CHECK(location == "Host Serial");
                extension_located = true;
            }
        }
        CHECK(extension_located);
    }

    SECTION("a sideways slot by its slot number and a floppy drive by its number") {
        auto preset_filepath = temp_dir.path() / "missing.preset.beebium";
        std::ofstream(preset_filepath) << R"({"model": "model-b",
            "storage": {"floppy_drives": [{"drive": 1,
                                           "image_uri": "file:///nonexistent/beebium.ssd"}]},
            "sideways_bank": {"slots": [{"slot": 4, "type": "rom",
                                         "image_uri": "no-such-beebium.rom"}]}})";
        const auto locations =
            locations_by_path(resolve_json(EXECUTABLE, preset_filepath.string()));
        INFO(nlohmann::json(locations).dump());
        CHECK(locations.at("/storage/floppy_drives/1/image_uri") == "Floppy drive 1");
        CHECK(locations.at("/sideways_bank/slots/0/image_uri") == "Sideways slot 4");
    }

    SECTION("a section by its schema label") {
        auto preset_filepath = temp_dir.path() / "station.preset.beebium";
        std::ofstream(preset_filepath) << R"({"model": "model-b", "econet": {"station": 300}})";
        const auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);
        const auto locations = locations_by_path(out);
        INFO(nlohmann::json(locations).dump());
        REQUIRE(locations.contains("/econet/station"));
        CHECK(locations.at("/econet/station") == "Econet");
    }

    SECTION("the whole document nowhere") {
        const auto out = resolve_json(
            EXECUTABLE, (temp_dir.path() / "absent.preset.beebium").string(), 66);
        REQUIRE(out.at("diagnostics").size() == 1);
        CHECK(out.at("diagnostics")[0].at("location") == "");
    }
}

TEST_CASE("describe-preset-schema labels each section and floppy drive",
          "[integration][preset][describe-preset-schema]") {
    auto result = run_command(std::string(EXECUTABLE) + " describe-preset-schema");
    REQUIRE(result.exit_code == 0);
    const auto schema = nlohmann::json::parse(result.stdout_output);
    std::map<std::string, nlohmann::json> sections;
    for (const auto& section : schema.at("sections")) {
        CHECK_FALSE(section.at("label").get<std::string>().empty());
        sections[section.at("type").get<std::string>()] = section;
    }
    CHECK(sections.at("sideways_bank").at("label") == "Sideways ROM / RAM");
    CHECK(sections.at("startup").at("label") == "Keyboard links");
    CHECK(sections.at("storage").at("floppy_drives")[1].at("label") == "Floppy drive 1");
}

TEST_CASE("describe-preset-schema names every kind of bundled image a parameter may name",
          "[integration][preset][describe-preset-schema]") {
    auto result = run_command(std::string(EXECUTABLE) + " describe-preset-schema");
    REQUIRE(result.exit_code == 0);
    const auto schema = nlohmann::json::parse(result.stdout_output);
    nlohmann::json extensions;
    for (const auto& section : schema.at("sections")) {
        if (section.at("type") == "extensions") extensions = section;
    }
    REQUIRE(extensions.is_object());

    std::map<std::string, std::string> nouns;
    for (const auto& k : extensions.at("bundled_image_kinds")) {
        nouns[k.at("kind").get<std::string>()] = k.at("display_name").get<std::string>();
    }
    // Plugins' ROMs only: hard discs travel in the presets that use them.
    CHECK(nouns == std::map<std::string, std::string>{{"rom", "ROM"}});

    // Every kind bundled_images lists, and every kind a parameter names, is named.
    for (const auto& [kind, images] : extensions.at("bundled_images").items()) {
        INFO(kind);
        CHECK(nouns.contains(kind));
    }
    for (const auto& ext : extensions.at("catalogue")) {
        for (const auto& p : ext.at("parameters")) {
            const auto bundled = p.at("bundled").get<std::string>();
            INFO(ext.at("name") << " " << p.at("key"));
            if (!bundled.empty()) CHECK(nouns.contains(bundled));
        }
    }
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

TEST_CASE("resolve-preset: a preset with three independent errors reports all three",
          "[integration][preset][resolve-preset][diagnostics]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "faulty.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "speed": "fast",
        "startup": {"screen_mode": 9}, "econet": {"station": 0}})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);

    CHECK(out.at("preset").is_null());
    std::vector<std::string> paths;
    for (const auto& d : out.at("diagnostics")) {
        CHECK(d.at("severity") == "error");
        paths.push_back(d.at("path").get<std::string>());
    }
    CHECK(paths == std::vector<std::string>{"/speed", "/startup/screen_mode", "/econet/station"});
    CHECK(out.at("diagnostics")[1].at("location") == "Keyboard links");
}

TEST_CASE("start: a preset with three independent errors names all three and exits CONFIG",
          "[integration][preset][diagnostics]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "faulty.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "speed": "fast",
        "startup": {"screen_mode": 9}, "econet": {"station": 0}})";
    auto result = run_command(EXECUTABLE + " start --preset \"" + preset_filepath.string() +
                              "\" --port 0");
    INFO(result.stderr_output);
    CHECK(result.exit_code == 78);
    CHECK(result.stderr_output.find("'speed'") != std::string::npos);
    CHECK(result.stderr_output.find("'screen_mode'") != std::string::npos);
    CHECK(result.stderr_output.find("between 1 and 254") != std::string::npos);
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
        // Stored with the files the shipped preset carries (a disc, say),
        // which the copy takes with it.
        const auto resolved_document = first.at("preset").dump(2);
        std::vector<beebium::zip::ZipMember> members{
            {"preset.json",
             std::vector<std::uint8_t>(resolved_document.begin(), resolved_document.end())}};
        beebium::zip::ZipReader shipped(preset_filepath);
        for (const auto& name : shipped.member_names()) {
            if (name != "preset.json") members.push_back({name, shipped.read(name)});
        }
        beebium::zip::write_zip(user_dir.presets() / "resolved.beebium-preset", std::move(members));
        auto saved_filepath = user_dir.path() / "saved.beebium-preset";
        const std::vector<std::pair<std::string, std::string>> env{
            {"BEEBIUM_USER_DIRPATH", user_dir.path().string()}};
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
        auto png_filepath = user_dir.presets() / "saved.png";
        auto built = run_command(
            executable + " capture-screenshot --preset \"" + saved_filepath.string() +
                "\" --duration 0 --output \"" + png_filepath.string() + "\"");
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

namespace {

// Whether `text` reads as a sentence: a capital first, a full stop last, and
// nothing outside 7-bit ASCII.
bool is_a_sentence(const std::string& text) {
    if (text.size() < 2) return false;
    if (std::any_of(text.begin(), text.end(),
                    [](char c) { return static_cast<unsigned char>(c) > 0x7F; })) {
        return false;
    }
    return std::isupper(static_cast<unsigned char>(text.front())) && text.back() == '.';
}

// The extensions section's catalogue, by extension name.
std::map<std::string, nlohmann::json> catalogue_by_name(const nlohmann::json& extensions) {
    std::map<std::string, nlohmann::json> by_name;
    for (const auto& e : extensions.at("catalogue")) {
        by_name[e.at("name").get<std::string>()] = e;
    }
    return by_name;
}

}  // namespace

TEST_CASE("describe-preset-schema: each attachment point says what it is",
          "[integration][preset][describe-preset-schema]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    for (const auto& point : extensions.at("attachment_points")) {
        INFO(point.at("id"));
        CHECK(is_a_sentence(point.at("description").get<std::string>()));
    }
}

TEST_CASE("describe-preset-schema: the links byte's eight bits, once each",
          "[integration][preset][describe-preset-schema][links]") {
    auto links = schema_section(describe_schema(EXECUTABLE), "startup").at("links");
    const auto& bits = links.at("bits");
    REQUIRE(bits.size() == 8);
    std::set<int> seen;
    for (const auto& entry : bits) {
        const int bit = entry.at("bit").get<int>();
        INFO(bit);
        CHECK(seen.insert(bit).second);
        CHECK(bit >= 0);
        CHECK(bit <= 7);
        // The Advanced User Guide numbers the links from bit 7 down.
        CHECK(entry.at("link") == "Link " + std::to_string(8 - bit));
        CHECK_FALSE(entry.at("label").get<std::string>().empty());
        CHECK(is_a_sentence(entry.at("description").get<std::string>()));
    }
}

TEST_CASE("describe-preset-schema: the links layout converts as the machine does",
          "[integration][preset][describe-preset-schema][links]") {
    auto links = schema_section(describe_schema(EXECUTABLE), "startup").at("links");
    CHECK(links.at("link_made_when") == "clear");
    const auto mode_bits = links.at("screen_mode_bits").get<std::vector<int>>();
    const int boot_bit = links.at("auto_boot_bit").get<int>();
    const bool boot_when_clear = links.at("auto_boot_true_when") == "clear";

    // The byte screen_mode and auto_boot give, built from the layout alone.
    auto from_layout = [&](int mode, bool auto_boot) {
        int byte = links.at("default").get<int>();
        for (std::size_t i = 0; i < mode_bits.size(); ++i) {
            const int mask = 1 << mode_bits[i];
            byte = ((mode >> i) & 1) ? (byte | mask) : (byte & ~mask);
        }
        const bool set = auto_boot != boot_when_clear;
        byte = set ? (byte | (1 << boot_bit)) : (byte & ~(1 << boot_bit));
        return byte;
    };

    for (int mode = 0; mode <= 7; ++mode) {
        for (bool auto_boot : {false, true}) {
            INFO("mode " << mode << " auto_boot " << auto_boot);
            beebium::KeyboardMatrix matrix;
            matrix.set_screen_mode(static_cast<uint8_t>(mode));
            matrix.set_auto_boot(auto_boot);
            CHECK(from_layout(mode, auto_boot) == matrix.startup_options());
        }
    }
}

TEST_CASE("describe-preset-schema: a catalogue of the extensions the model can take",
          "[integration][preset][describe-preset-schema][catalogue]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    auto catalogue = catalogue_by_name(extensions);

    // Built in and plugin alike, peripherals and transports alike.
    REQUIRE(catalogue.count("host-serial") == 1);
    CHECK(catalogue["host-serial"].at("display_name") == "Host Serial");
    CHECK(catalogue["host-serial"].at("attaches_to") == nlohmann::json::array({"serial-port"}));
    REQUIRE(catalogue.count("acorn-scsi") == 1);
    CHECK(catalogue["acorn-scsi"].at("provides") == nlohmann::json::array({"scsi"}));
    REQUIRE(catalogue.count("aun") == 1);
    CHECK(catalogue["aun"].at("kind") == "econet-transport");

    // Every extension an attachment point lists is described.
    for (const auto& point : extensions.at("attachment_points")) {
        for (const auto& name : point.at("extensions")) {
            CHECK(catalogue.count(name.get<std::string>()) == 1);
        }
    }

    // Each entry is exactly what describe-extension --json gives.
    for (const char* name : {"host-serial", "acorn-scsi", "acorn-65c02-coprocessor"}) {
        INFO(name);
        auto result = run_command(EXECUTABLE + " describe-extension " + name + " --json");
        REQUIRE(result.exit_code == 0);
        CHECK(catalogue[name] == nlohmann::json::parse(result.stdout_output));
    }
}

TEST_CASE("describe-preset-schema: every extension describes itself in a sentence",
          "[integration][preset][describe-preset-schema][catalogue]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    for (const auto& [name, entry] : catalogue_by_name(extensions)) {
        INFO(name);
        CHECK_FALSE(entry.at("display_name").get<std::string>().empty());
        CHECK(is_a_sentence(entry.at("description").get<std::string>()));
    }
}

TEST_CASE("describe-preset-schema: every bundled image has a display name",
          "[integration][preset][describe-preset-schema][bundled]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    std::map<std::string, std::string> display_names;
    for (const auto& [kind, images] : extensions.at("bundled_images").items()) {
        for (const auto& image : images) {
            display_names[image.at("image").get<std::string>()] =
                image.at("display_name").get<std::string>();
        }
    }
    CHECK(display_names.at("acorn-tube-6502_1_10.rom") == "Acorn 6502 Tube ROM 1.10");
    CHECK(display_names.at("acorn-tube-65c102_1_20.rom") == "Acorn 65C102 Tube ROM 1.20");
}

TEST_CASE("describe-preset-schema: a coprocessor's ROM defaults to the one it ships",
          "[integration][preset][describe-preset-schema][bundled]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    auto catalogue = catalogue_by_name(extensions);
    for (const char* name : {"acorn-65c02-coprocessor", "acorn-65c102-coprocessor"}) {
        INFO(name);
        std::string rom_default;
        for (const auto& p : catalogue.at(name).at("parameters")) {
            if (p.at("key") == "rom") rom_default = p.at("default").get<std::string>();
        }
        bool shipped = false;
        for (const auto& rom : extensions.at("bundled_images").at("rom")) {
            if (rom.at("image") == rom_default && rom.at("extension") == name) shipped = true;
        }
        CHECK(shipped);
    }
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
// An extension's own check of its configuration, before launch
// ============================================================================

namespace {

// The diagnostics resolve-preset gives a preset, as (severity, path, message).
std::vector<std::tuple<std::string, std::string, std::string>> diagnostics_of(
        const std::string& body, int expected_exit_code) {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "rtc.preset.beebium";
    std::ofstream(preset_filepath) << body;
    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), expected_exit_code);
    std::vector<std::tuple<std::string, std::string, std::string>> found;
    for (const auto& d : out.at("diagnostics")) {
        found.emplace_back(d.at("severity").get<std::string>(), d.at("path").get<std::string>(),
                           d.at("message").get<std::string>());
    }
    return found;
}

}  // namespace

TEST_CASE("resolve-preset: a start time the RTC's layout cannot hold is an error at the time",
          "[integration][preset][resolve-preset][validate]") {
    auto found = diagnostics_of(R"({"model": "model-b", "extensions": [
        {"name": "acorn-rtc", "config": {"time": "2001-01-01T00:00",
                                         "layout": "4bit-year-in-r1"}}]})", 78);
    REQUIRE(found.size() == 1);
    const auto& [severity, path, message] = found[0];
    CHECK(severity == "error");
    CHECK(path == "/extensions/0/config/time");
    INFO(message);
    CHECK(message.find("1981-1996") != std::string::npos);
}

TEST_CASE("resolve-preset: an RTC offset that would start out of range today is an error",
          "[integration][preset][resolve-preset][validate]") {
    auto found = diagnostics_of(R"({"model": "model-b", "extensions": [
        {"name": "acorn-rtc", "config": {"offset": "+100y"}}]})", 78);
    REQUIRE(found.size() == 1);
    const auto& [severity, path, message] = found[0];
    CHECK(severity == "error");
    CHECK(path == "/extensions/0/config/offset");
    CHECK(message.find("1981-2099") != std::string::npos);
}

TEST_CASE("resolve-preset: an RTC on host time with the default layout has no diagnostics",
          "[integration][preset][resolve-preset][validate]") {
    CHECK(diagnostics_of(R"({"model": "model-b", "extensions": [
        {"name": "acorn-rtc"}]})", 0).empty());
}

TEST_CASE("resolve-preset: an RTC on host time its layout cannot hold is an error on the entry",
          "[integration][preset][resolve-preset][validate]") {
    auto found = diagnostics_of(R"({"model": "model-b", "extensions": [
        {"name": "acorn-rtc", "config": {"layout": "4bit-year-in-r1"}}]})", 78);
    REQUIRE(found.size() == 1);
    CHECK(std::get<0>(found[0]) == "error");
    CHECK(std::get<1>(found[0]) == "/extensions/0");
}

TEST_CASE("resolve-preset: an RTC configuration its layout holds has no diagnostics",
          "[integration][preset][resolve-preset][validate]") {
    CHECK(diagnostics_of(R"({"model": "model-b", "extensions": [
        {"name": "acorn-rtc", "config": {"time": "1985-03-15T10:30"}}]})", 0).empty());
}

TEST_CASE("start: a start time the RTC's layout cannot hold stops the launch with CONFIG",
          "[integration][preset][validate]") {
    auto result = run_command(EXECUTABLE +
                              " start --acorn-rtc layout=4bit-year-in-r1:time=2001-01-01T0000"
                              " --port 0");
    CHECK(result.exit_code == 78);
    INFO(result.stderr_output);
    CHECK(result.stderr_output.find("1981-1996") != std::string::npos);
    // Refused before the machine is built, not by the extension at start-up.
    CHECK(result.stderr_output.find("Initialising extensions") == std::string::npos);
    CHECK(result.stdout_output.find("Initialising extensions") == std::string::npos);
}

// ============================================================================
// A hard disc is a medium a parameter names, not a bundled image
// ============================================================================

TEST_CASE("describe-preset-schema: the bundled images are plugins' ROMs, never hard discs",
          "[integration][preset][describe-preset-schema][bundled]") {
    auto extensions = schema_section(describe_schema(EXECUTABLE), "extensions");
    std::vector<std::string> kinds;
    for (const auto& [kind, images] : extensions.at("bundled_images").items()) {
        kinds.push_back(kind);
    }
    CHECK(kinds == std::vector<std::string>{"rom"});
}

TEST_CASE("describe-extension: a parameter that names a medium says which kind",
          "[integration][extension][describe-extension][media]") {
    auto result = run_command(EXECUTABLE + " --format jsonl describe-extension scsi-hard-disc");
    REQUIRE(result.exit_code == 0);
    std::istringstream lines(result.stdout_output);
    std::string line;
    bool image_seen = false;
    while (std::getline(lines, line)) {
        auto p = nlohmann::json::parse(line);
        CHECK(p.at("bundled") == "");
        if (p.at("key") == "image") {
            image_seen = true;
            CHECK(p.at("media") == "hard-disc");
        } else {
            CHECK(p.at("media") == "");
        }
    }
    CHECK(image_seen);
}

TEST_CASE("resolve-preset: a bare hard-disc name found nowhere is a warning",
          "[integration][preset][resolve-preset][media]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "nope.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"image": "no-such-disc.dat"}}]})";

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
// File references at launch: the one rule, through the real server
// ============================================================================

namespace {

// The file called `name` the build gives the server, found by walking up from
// the executable's directory into `subdirpath` (roms/, discs/, or a plugin's
// roms/ under extensions/).
std::filesystem::path built_file(const std::filesystem::path& subdirpath,
                                 const std::string& name) {
    auto dirpath = std::filesystem::absolute(std::filesystem::path(EXECUTABLE)).parent_path();
    for (int level = 0; level < 6; ++level) {
        if (std::filesystem::exists(dirpath / subdirpath / name)) {
            return std::filesystem::canonical(dirpath / subdirpath / name);
        }
        dirpath = dirpath.parent_path();
    }
    FAIL("the build has no " << (subdirpath / name).string());
    return {};
}

// Runs `command` with the working directory set to `dirpath`.
ProcessResult run_command_in(const std::filesystem::path& dirpath, const std::string& command) {
    const auto saved_cwd = std::filesystem::current_path();
    std::filesystem::current_path(dirpath);
    auto result = run_command(command);
    std::filesystem::current_path(saved_cwd);
    return result;
}

// Writes the Level 3 File Server hard disc the shipped L3FS preset carries
// to `dat_filepath`, with its geometry beside it as the .dsc.
void put_l3fs_disc(const std::filesystem::path& dat_filepath) {
    beebium::zip::ZipReader preset(built_file("presets", "model-b-l3fs-aun.beebium-preset"));
    preset.extract("l3fs-v1_26b.dat", dat_filepath);
    preset.extract("l3fs-v1_26b.dsc", std::filesystem::path(dat_filepath).replace_extension(".dsc"));
}

std::string capture_command(const std::filesystem::path& png_filepath) {
    return EXECUTABLE + " capture-screenshot --duration 0 --output \"" +
           png_filepath.string() + "\"";
}

}  // namespace

TEST_CASE("a coprocessor ROM in the per-user roms/ takes the place of the plugin's",
          "[integration][preset][bundled][resource_paths]") {
    TempDirectory temp_dir;
    const auto user_dirpath = temp_dir.path() / "user";
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user_dirpath.string());
    const auto shipped = built_file(std::filesystem::path("extensions") /
                                        "acorn-65c102-coprocessor" / "roms",
                                    "acorn-tube-65c102_1_20.rom");
    std::filesystem::create_directories(user_dirpath / "roms");
    std::filesystem::copy_file(shipped, user_dirpath / "roms" / "acorn-tube-65c102_1_20.rom");

    auto result = run_command(capture_command(temp_dir.path() / "rom.png") +
                              " --tube-65c102 rom=acorn-tube-65c102_1_20.rom");
    INFO(result.stdout_output << result.stderr_output);
    CHECK(result.exit_code == 0);
    // The notice names both files.
    CHECK(result.stderr_output.find(
              (user_dirpath / "roms" / "acorn-tube-65c102_1_20.rom").string()) !=
          std::string::npos);
    CHECK(result.stderr_output.find(shipped.string()) != std::string::npos);

    // The manifest still decides which names may be given: a ROM it does not
    // declare is refused, wherever a file of that name is.
    std::filesystem::copy_file(shipped, user_dirpath / "roms" / "not-declared.rom");
    auto refused = run_command(capture_command(temp_dir.path() / "rom.png") +
                               " --tube-65c102 rom=not-declared.rom");
    CHECK(refused.exit_code == 78);
}

TEST_CASE("a coprocessor ROM beside the preset is the one it loads",
          "[integration][preset][bundled][resource_paths]") {
    TempDirectory temp_dir;
    const auto shipped = built_file(std::filesystem::path("extensions") /
                                        "acorn-65c102-coprocessor" / "roms",
                                    "acorn-tube-65c102_1_20.rom");
    std::filesystem::copy_file(shipped, temp_dir.path() / "acorn-tube-65c102_1_20.rom");
    std::filesystem::create_directories(temp_dir.path() / "user" / "roms");
    std::filesystem::copy_file(shipped,
                               temp_dir.path() / "user" / "roms" / "acorn-tube-65c102_1_20.rom");
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH",
                                      (temp_dir.path() / "user").string());
    const auto preset_filepath = temp_dir.path() / "copro.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-65c102-coprocessor", "config": {"rom": "acorn-tube-65c102_1_20.rom"}}]})";

    auto result = run_command(capture_command(temp_dir.path() / "rom.png") + " --preset \"" +
                              preset_filepath.string() + "\"");
    INFO(result.stdout_output << result.stderr_output);
    CHECK(result.exit_code == 0);
    // The container's file comes before the per-user one, so nothing is noted.
    CHECK(result.stderr_output.find("takes the place of") == std::string::npos);
}

TEST_CASE("a command line's bare MOS and sideways names are found in the working directory",
          "[integration][resource_paths][cli]") {
    TempDirectory temp_dir;
    std::filesystem::copy_file(built_file("roms", "acorn-mos_1_20.rom"),
                               temp_dir.path() / "cwd-mos.rom");
    std::filesystem::copy_file(built_file("roms", "acorn-dfs_2_26.rom"),
                               temp_dir.path() / "cwd-dfs.rom");

    auto result = run_command_in(
        temp_dir.path(), capture_command(temp_dir.path() / "cwd.png") +
                             " --mos cwd-mos.rom --sideways slot=14:type=rom:image=cwd-dfs.rom");
    INFO(result.stdout_output << result.stderr_output);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output.find("cwd-mos.rom") != std::string::npos);
    CHECK(result.stdout_output.find("cwd-dfs.rom") != std::string::npos);
}

TEST_CASE("a command line's bare hard-disc name is found in the working directory",
          "[integration][resource_paths][cli]") {
    TempDirectory temp_dir;
    const std::string scsi = " --acorn-scsi --scsi-hdd scsi-id=0:image=cwd-disc.dat";
    auto missing = run_command_in(temp_dir.path(),
                                  capture_command(temp_dir.path() / "hd.png") + scsi);
    CHECK(missing.exit_code == 78);
    CHECK(missing.stderr_output.find("searched " + temp_dir.path().string()) !=
          std::string::npos);

    put_l3fs_disc(temp_dir.path() / "cwd-disc.dat");
    auto found = run_command_in(temp_dir.path(),
                                capture_command(temp_dir.path() / "hd.png") + scsi);
    INFO(found.stdout_output << found.stderr_output);
    CHECK(found.exit_code == 0);
}

TEST_CASE("a preset's relative and bare per-user hard-disc images are both found",
          "[integration][preset][resource_paths]") {
    // End to end: a path relative to the preset, and a bare name the per-user
    // discs/ holds, through a launch from the preset.
    TempDirectory temp_dir;
    const auto user_dirpath = temp_dir.path() / "user";
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user_dirpath.string());
    std::filesystem::create_directories(temp_dir.path() / "pack" / "discs");
    std::filesystem::create_directories(user_dirpath / "discs");
    put_l3fs_disc(temp_dir.path() / "pack" / "discs" / "relative.dat");
    const auto preset_filepath = temp_dir.path() / "pack" / "discs.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "image": "discs/relative.dat"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 1, "image": "user-disc.dat"}}]})";
    const auto command = capture_command(temp_dir.path() / "e2e.png") + " --preset \"" +
                         preset_filepath.string() + "\"";

    auto before = run_command(command);
    CHECK(before.exit_code == 78);
    CHECK(before.stderr_output.find("user-disc.dat' not found") != std::string::npos);

    put_l3fs_disc(user_dirpath / "discs" / "user-disc.dat");
    auto after = run_command(command);
    INFO(after.stdout_output << after.stderr_output);
    CHECK(after.exit_code == 0);
}

TEST_CASE("a launch copies a template's disc into its own directory and removes it after",
          "[integration][preset][resource_paths][machine_directory]") {
    // The L3FS disc beside the preset, named by its bare name, is copied into
    // the machine's directory under machines/ -- never into the per-user
    // discs/, and never written in place -- and the directory is gone once the
    // capture has shut the machine down.
    TempDirectory temp_dir;
    const auto user_dirpath = temp_dir.path() / "user";
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user_dirpath.string());
    put_l3fs_disc(temp_dir.path() / "l3fs-v1_26b.dat");
    const auto preset_filepath = temp_dir.path() / "fs.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b", "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "image": "l3fs-v1_26b.dat"}}]})";

    auto result = run_command(capture_command(temp_dir.path() / "fs.png") + " --preset \"" +
                              preset_filepath.string() + "\"");
    INFO(result.stdout_output << result.stderr_output);
    CHECK(result.exit_code == 0);
    CHECK(std::filesystem::is_empty(user_dirpath / "machines"));
    CHECK_FALSE(std::filesystem::exists(user_dirpath / "discs"));
}

namespace {

std::vector<std::uint8_t> file_bytes(const std::filesystem::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// A member of the shipped L3FS preset (its disc), read.
std::vector<std::uint8_t> l3fs_member(const std::string& name) {
    return beebium::zip::ZipReader(built_file("presets", "model-b-l3fs-aun.beebium-preset"))
        .read(name);
}

}  // namespace

TEST_CASE("a launch from a preset archive takes its discs and ROMs from inside it",
          "[integration][preset][resource_paths][preset_archive]") {
    // Everything the preset names is in the archive: a hard disc and its
    // geometry by path, a sideways ROM by path, and the coprocessor's ROM by
    // bare name. Nothing is found anywhere else, so the launch succeeds only
    // if each is taken from inside.
    TempDirectory temp_dir;
    const auto user_dirpath = temp_dir.path() / "user";
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user_dirpath.string());
    const std::string document = R"({"model": "model-b",
        "sideways_bank": {"slots": [{"slot": 14, "type": "rom", "image_uri": "roms/dfs.rom"}]},
        "extensions": [
            {"name": "acorn-scsi"},
            {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "image": "discs/fs.dat"}},
            {"name": "acorn-65c102-coprocessor",
             "config": {"rom": "acorn-tube-65c102_1_20.rom"}}]})";
    const auto archive_filepath = temp_dir.path() / "everything.beebium-preset";
    beebium::zip::write_zip(
        archive_filepath,
        {{"preset.json", std::vector<std::uint8_t>(document.begin(), document.end())},
         {"discs/fs.dat", l3fs_member("l3fs-v1_26b.dat")},
         {"discs/fs.dsc", l3fs_member("l3fs-v1_26b.dsc")},
         {"roms/dfs.rom", file_bytes(built_file("roms", "acorn-dfs_2_26.rom"))},
         {"acorn-tube-65c102_1_20.rom", file_bytes(built_file(std::filesystem::path("extensions") /
                                                 "acorn-65c102-coprocessor" / "roms",
                                             "acorn-tube-65c102_1_20.rom"))}});

    auto resolved = run_command(EXECUTABLE + " resolve-preset \"" + archive_filepath.string() +
                                "\" --json");
    INFO(resolved.stdout_output << resolved.stderr_output);
    CHECK(resolved.exit_code == 0);
    CHECK(nlohmann::json::parse(resolved.stdout_output).at("diagnostics").empty());

    auto launched = run_command(capture_command(temp_dir.path() / "everything.png") +
                                " --preset \"" + archive_filepath.string() + "\"");
    INFO(launched.stdout_output << launched.stderr_output);
    CHECK(launched.exit_code == 0);
    // The sideways ROM was read from the archive; the hard disc was copied,
    // and the coprocessor's ROM extracted, into the machine's directory (each
    // at its own relative path), which is gone after.
    std::ostringstream sideways_rom;  // as load_roms prints it
    sideways_rom << (archive_filepath / "roms" / "dfs.rom").lexically_normal();
    CHECK(launched.stdout_output.find(sideways_rom.str()) != std::string::npos);
    const auto machines = (user_dirpath / "machines").string();
    CHECK(launched.stdout_output.find("image=" + machines) != std::string::npos);
    CHECK(launched.stdout_output.find("rom=" + machines) != std::string::npos);
    CHECK(std::filesystem::is_empty(user_dirpath / "machines"));
}

TEST_CASE("show-preset reads a preset archive's document and thumbnail",
          "[integration][preset][show-preset][preset_archive]") {
    TempDirectory temp_dir;
    const std::string document = R"({"model": "model-b-plus", "name": "Zipped"})";
    const auto archive_filepath = temp_dir.path() / "zipped.beebium-preset";
    beebium::zip::write_zip(
        archive_filepath,
        {{"preset.json", std::vector<std::uint8_t>(document.begin(), document.end())},
         {"thumbnail.png", {'P', 'N', 'G'}}});

    auto shown = run_command(EXECUTABLE + " show-preset \"" + archive_filepath.string() + "\"");
    REQUIRE(shown.exit_code == 0);
    CHECK(shown.stdout_output == document);

    const auto png = temp_dir.path() / "thumb.png";
    auto thumbnail = run_command(EXECUTABLE + " show-preset \"" + archive_filepath.string() +
                                 "\" --thumbnail --output \"" + png.string() + "\"");
    REQUIRE(thumbnail.exit_code == 0);
    CHECK(file_bytes(png) == std::vector<std::uint8_t>{'P', 'N', 'G'});
}

TEST_CASE("create-preset --from: the copy holds the source's files, not its thumbnail",
          "[integration][preset][create-preset][preset_archive]") {
    TempDirectory user_dir;
    const std::string document = R"({"name": "Source", "model": "model-b",
        "sideways_bank": {"slots": [{"slot": 14, "type": "rom", "image_uri": "roms/dfs.rom"}]}})";
    std::ifstream dfs(built_file("roms", "acorn-dfs_2_26.rom"), std::ios::binary);
    beebium::zip::write_zip(
        user_dir.presets() / "source.beebium-preset",
        {{"preset.json", std::vector<std::uint8_t>(document.begin(), document.end())},
         {"roms/dfs.rom", std::vector<std::uint8_t>(std::istreambuf_iterator<char>(dfs), {})},
         {"thumbnail.png", {'P', 'N', 'G'}}});

    auto result = run_command(EXECUTABLE + " create-preset --from source --name Copy",
                              {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});

    REQUIRE(result.exit_code == 0);
    beebium::zip::ZipReader copy(user_dir.presets() / "copy.beebium-preset");
    CHECK(copy.member_names() == std::vector<std::string>{"preset.json", "roms/dfs.rom"});
    // ...and names its own copy of the ROM, as the source named its.
    const auto copied = nlohmann::json::parse(document_of(user_dir.presets() /
                                                            "copy.beebium-preset"));
    CHECK(copied.at("sideways_bank").at("slots")[0].at("image_uri") == "roms/dfs.rom");
    // The copy stands alone: with the source gone, its ROM reference still
    // reaches the ROM inside it.
    std::filesystem::remove(user_dir.presets() / "source.beebium-preset");
    auto resolved = run_command(EXECUTABLE + " resolve-preset copy --json",
                                {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});
    INFO(resolved.stdout_output << resolved.stderr_output);
    CHECK(resolved.exit_code == 0);
    CHECK(nlohmann::json::parse(resolved.stdout_output).at("diagnostics").empty());
}

TEST_CASE("the shipped L3FS preset carries its hard disc and needs nothing beside it",
          "[integration][preset][shipped][preset_archive]") {
    const auto archive_filepath = std::filesystem::absolute(std::filesystem::path(EXECUTABLE))
                                      .parent_path() / "presets" / "model-b-l3fs-aun.beebium-preset";
    beebium::zip::ZipReader archive(archive_filepath);
    CHECK(archive.member_names() ==
          std::vector<std::string>{"l3fs-v1_26b.dat", "l3fs-v1_26b.dsc", "preset.json",
                                   "thumbnail.png"});
    CHECK(archive.read("l3fs-v1_26b.dat").size() == 10002432);  // the Level 3 image

    // With nothing anywhere else, the disc is found in the preset.
    TempDirectory temp_dir;
    auto resolved = run_command(
        EXECUTABLE + " resolve-preset \"" + archive_filepath.string() + "\" --json",
        {{"BEEBIUM_USER_DIRPATH", temp_dir.path().string()}});
    INFO(resolved.stdout_output << resolved.stderr_output);
    CHECK(resolved.exit_code == 0);
    for (const auto& d : nlohmann::json::parse(resolved.stdout_output).at("diagnostics")) {
        CHECK(d.at("message").get<std::string>().find("l3fs") == std::string::npos);
    }
}

TEST_CASE("create-preset records a bare name the working directory holds as its path",
          "[integration][preset][create-preset][resource_paths]") {
    // What the command line meant: the file in the working directory. A bare
    // name found per-user means the search, which survives the preset moving.
    TempDirectory temp_dir;
    const auto cwd = temp_dir.path() / "cwd";
    const auto user_dirpath = temp_dir.path() / "user";
    std::filesystem::create_directories(cwd);
    std::filesystem::create_directories(user_dirpath / "discs");
    std::filesystem::create_directories(user_dirpath / "roms");
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user_dirpath.string());
    std::ofstream(cwd / "here.ssd") << "disc";
    std::ofstream(cwd / "here.rom") << "rom";
    std::ofstream(user_dirpath / "discs" / "mine.ssd") << "disc";
    std::ofstream(user_dirpath / "roms" / "mine.rom") << "rom";
    const auto output_filepath = temp_dir.path() / "elsewhere" / "p.beebium-preset";
    std::filesystem::create_directories(output_filepath.parent_path());

    auto result = run_command_in(
        cwd, EXECUTABLE + " create-preset --name Meant --fdc acorn-1770"
                          " --floppy 0:here.ssd --floppy 1:mine.ssd --mos here.rom"
                          " --sideways slot=14:type=rom:image=mine.rom --output \"" +
                 output_filepath.string() + "\"");
    INFO(result.stdout_output << result.stderr_output);
    REQUIRE(result.exit_code == 0);

    const auto preset = nlohmann::json::parse(document_of(output_filepath));
    const auto& drives = preset.at("storage").at("floppy_drives");
    CHECK(drives[0].at("image_uri") == beebium::DiscUrl::from_filepath(cwd / "here.ssd").url());
    CHECK(drives[1].at("image_uri") == "mine.ssd");
    CHECK(preset.at("mos_image_uri") == (cwd / "here.rom").string());
    CHECK(preset.at("sideways_bank").at("slots")[0].at("image_uri") == "mine.rom");
}

TEST_CASE("an AUN map file not yet made is a location, not a search",
          "[integration][resource_paths][aun]") {
    // The transport makes its map file on the first edit, so a map-file path
    // that does not exist yet is resolved from where it is written and handed
    // over, never searched for and never an error.
    TempDirectory temp_dir;
    auto result = run_command_in(
        temp_dir.path(), capture_command(temp_dir.path() / "aun.png") +
                             " --station 2 --aun port=none:discovery=off:map-file=maps/new.json");
    INFO(result.stdout_output << result.stderr_output);
    CHECK(result.exit_code == 0);
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
    return nlohmann::json::parse(document_of(filepath));
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

TEST_CASE("create-preset: every option that configures the launch is refused",
          "[integration][preset][create-preset][given]") {
    const auto [option, flag] = GENERATE(table<std::string, std::string>({
        {"--port 4000", "--port"},
        {"--wait=api", "--wait"},
        {"--wait", "--wait"},
        {"--advertise", "--advertise"},
        {"--allow-shutdown", "--allow-shutdown"},
        {"--machine-uuid 12345678-1234-1234-1234-123456789abc", "--machine-uuid"},
        {"--machine-ordinal 2", "--machine-ordinal"},
        {"--provenance-type test", "--provenance-type"},
        {"--provenance-uuid 12345678-1234-1234-1234-123456789abc", "--provenance-uuid"},
        {"--provenance-version 1", "--provenance-version"},
        {"--rom-dir /tmp", "--rom-dir"},
    }));
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "launch.preset.beebium";
    auto result = run_command(EXECUTABLE + " create-preset --name \"Launch\" " + option +
                              " --output \"" + output_filepath.string() + "\"");
    INFO(option << "\n" << result.stderr_output);
    CHECK(result.exit_code == 64);
    CHECK(result.stderr_output.find(flag + " configures the launch, not the machine") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output_filepath));
}

TEST_CASE("create-preset: a machine option with no preset form is refused",
          "[integration][preset][create-preset][given]") {
    const auto [option, flag] = GENERATE(table<std::string, std::string>({
        {"--write-protect slot-15", "--write-protect"},
        {"--hide slot-15", "--hide"},
        {"--motherboard-link key=value", "--motherboard-link"},
    }));
    TempDirectory temp_dir;
    auto output_filepath = temp_dir.path() / "board.preset.beebium";
    auto result = run_command(EXECUTABLE + " create-preset --name \"Board\" " + option +
                              " --output \"" + output_filepath.string() + "\"");
    INFO(option << "\n" << result.stderr_output);
    CHECK(result.exit_code == 64);
    CHECK(result.stderr_output.find(flag + " has no preset form yet") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output_filepath));
}

TEST_CASE("create-preset --from: a flag overrides the source preset as start would",
          "[integration][preset][create-preset][given]") {
    TempDirectory user_dir;
    std::filesystem::create_directory(user_dir.path() / "discs");
    const auto source_filepath = user_dir.path() / "source.json";
    std::ofstream(source_filepath)
        << R"({"model": "model-b", "name": "Source", "description": "From here",
                 "storage": {"floppy_drives": [{"drive": 0, "image_uri": "discs/game.ssd"}]},
                 "extensions": [{"name": "host-serial",
                                 "config": {"id": "modem", "mode": "device",
                                            "path": "/dev/ttyUSB0", "baud": 9600}}]})";
    auto output_filepath = user_dir.path() / "copy.beebium-preset";
    auto result = run_command(
        EXECUTABLE + " create-preset --from \"" + source_filepath.string() + "\" --name \"Copy\" "
        "--host-serial id=modem:baud=2400 --output \"" + output_filepath.string() + "\"",
        {{"BEEBIUM_USER_DIRPATH", user_dir.path().string()}});
    INFO(result.stderr_output);
    REQUIRE(result.exit_code == 0);

    auto preset = read_json(output_filepath);
    CHECK(preset.at("name") == "Copy");
    CHECK(preset.at("description") == "From here");
    REQUIRE(preset.at("extensions").size() == 1);
    CHECK(preset.at("extensions")[0].at("config") ==
          nlohmann::json{{"baud", 2400}, {"id", "modem"}, {"mode", "device"},
                         {"path", "/dev/ttyUSB0"}});
    // The lone source's relative floppy meant the directory beside it, which
    // the copy does not take with it; the copy says so.
    CHECK(preset.at("storage").at("floppy_drives")[0].at("image_uri") ==
          beebium::server::normalize_image_uri(
              (std::filesystem::absolute(user_dir.path()) / "discs" / "game.ssd").string(), {}));
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
    "speed": 1.0,
    "startup": {
      "screen_mode": 7,
      "auto_boot": true
    },
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
      "location": "colour",
      "message": "unknown key 'colour' is ignored"
    },
    {
      "severity": "warning",
      "path": "/auto_boot",
      "location": "Auto boot",
      "message": "a top-level 'auto_boot' is read as 'startup.auto_boot'; write it there instead"
    },
    {
      "severity": "error",
      "path": "/extensions/0",
      "location": "Host Serial",
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
               {"location", "Econet"},
               {"message", "Econet station number must be between 1 and 254, got 300: " +
                               preset_filepath.string()}}})}};
    CHECK(result.stdout_output == expected.dump(2) + "\n");
}

// ============================================================================
// describe-preset-schema's output, pinned per model: front-ends build their
// configuration UI from it. What depends on the build -- which plugins it
// built (the extensions at each point, the points they provide, the Econet
// transports, the catalogue describing them) and the bundled images it
// decompressed -- is normalised out of both sides; everything else is
// compared byte for byte.
// ============================================================================

namespace {

nlohmann::ordered_json without_build_dependent_parts(nlohmann::ordered_json schema) {
    static const std::set<std::string> machine_points{"1mhz-bus", "user-port", "tube",
                                                      "serial-port"};
    for (auto& section : schema.at("sections")) {
        const auto type = section.at("type").get<std::string>();
        if (type == "econet") {
            section["transports"] = nlohmann::ordered_json::array();
        } else if (type == "extensions") {
            auto points = nlohmann::ordered_json::array();
            for (auto point : section.at("attachment_points")) {
                if (machine_points.count(point.at("id").get<std::string>()) == 0) continue;
                point["extensions"] = nlohmann::ordered_json::array();
                points.push_back(std::move(point));
            }
            section["attachment_points"] = std::move(points);
            section["catalogue"] = nlohmann::ordered_json::array();
            section.erase("bundled_images");
        }
    }
    return schema;
}

}  // namespace

TEST_CASE("describe-preset-schema output is stable",
          "[integration][preset][describe-preset-schema][pinned]") {
    for (const char* model : {"model-b", "model-b-plus", "model-b-integra-b"}) {
        INFO(model);
        auto result =
            run_command(find_executable(std::string("beebium-") + model).string() +
                        " describe-preset-schema");
        REQUIRE(result.exit_code == 0);
        std::ifstream expected_file(std::filesystem::path(BEEBIUM_TEST_ASSETS_DIR) /
                                    "describe_preset_schema" / (std::string(model) + ".json"));
        const auto expected = without_build_dependent_parts(
            nlohmann::ordered_json::parse(expected_file));
        const auto actual =
            without_build_dependent_parts(nlohmann::ordered_json::parse(result.stdout_output));
        CHECK(actual.dump(2) == expected.dump(2));
    }
}

// ============================================================================
// A list parameter with fields: items as objects or strings
// ============================================================================

namespace {

// Every error path resolving `body` gives.
std::vector<std::string> error_paths(const std::string& body) {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "items.preset.beebium";
    std::ofstream(preset_filepath) << body;
    auto out = resolve_json(EXECUTABLE, preset_filepath.string(), 78);
    std::vector<std::string> paths;
    for (const auto& d : out.at("diagnostics")) {
        if (d.at("severity") == "error") paths.push_back(d.at("path").get<std::string>());
    }
    return paths;
}

bool has_path(const std::vector<std::string>& paths, const std::string& path) {
    INFO("paths: " << nlohmann::json(paths).dump());
    return std::find(paths.begin(), paths.end(), path) != paths.end();
}

}  // namespace

TEST_CASE("resolve-preset: AUN peers and subnet rules come back in the form given",
          "[integration][preset][resolve-preset][list-item]") {
    TempDirectory temp_dir;
    auto preset_filepath = temp_dir.path() / "peers.preset.beebium";
    std::ofstream(preset_filepath) << R"({"model": "model-b",
        "econet": {"station": 32, "transport": {"name": "aun", "parameters": {
            "port": 0, "discovery": "off", "map-file": "none",
            "map": [{"net": 0, "station": 254, "host": "127.0.0.1", "port": 32769},
                    "253@127.0.0.1@32770",
                    {"station": 252, "host": "127.0.0.1", "port": 32771}],
            "subnet": [{"net": 128, "address": "192.168.5.0/24"}]}}}})";

    auto out = resolve_json(EXECUTABLE, preset_filepath.string());

    INFO(out.dump(2));
    CHECK(out.at("diagnostics").empty());
    const auto& parameters = out.at("preset").at("econet").at("transport").at("parameters");
    CHECK(parameters.at("map") ==
          nlohmann::json::parse(R"([
              {"net": 0, "station": 254, "host": "127.0.0.1", "port": 32769},
              "253@127.0.0.1@32770",
              {"station": 252, "host": "127.0.0.1", "port": 32771}])"));
    CHECK(parameters.at("subnet") ==
          nlohmann::json::parse(R"([{"net": 128, "address": "192.168.5.0/24"}])"));
}

TEST_CASE("resolve-preset: a bad field of an object item is located at the field",
          "[integration][preset][resolve-preset][list-item][pointers]") {
    CHECK(has_path(error_paths(R"({"model": "model-b",
        "econet": {"station": 32, "transport": {"name": "aun", "parameters": {
            "map": ["1@127.0.0.1@32769",
                    {"station": 300, "host": "127.0.0.1", "port": 32770}]}}}})"),
                   "/econet/transport/parameters/map/1/station"));
    CHECK(has_path(error_paths(R"({"model": "model-b", "extensions": [
        {"name": "aun", "config": {"subnet": [{"net": 0, "address": "10.0.0.0/24"}]}}]})"),
                   "/extensions/0/config/subnet/0/net"));
}

TEST_CASE("resolve-preset: a malformed string item is located at the item",
          "[integration][preset][resolve-preset][list-item][pointers]") {
    CHECK(has_path(error_paths(R"({"model": "model-b",
        "econet": {"station": 32, "transport": {"name": "aun", "parameters": {
            "map": ["0.254@127.0.0.1"]}}}})"),
                   "/econet/transport/parameters/map/0"));
}

TEST_CASE("describe-extension: the AUN map and subnet describe their items' fields",
          "[integration][extension][describe-extension][list-item]") {
    auto result = run_command(EXECUTABLE + " describe-extension aun --json");
    REQUIRE(result.exit_code == 0);
    auto manifest = nlohmann::json::parse(result.stdout_output);
    nlohmann::json map, subnet;
    for (const auto& p : manifest.at("parameters")) {
        if (p.at("key") == "map") map = p;
        if (p.at("key") == "subnet") subnet = p;
    }
    CHECK(map.at("item_format") == "{net}.{station}@{host}@{port}");
    REQUIRE(map.at("fields").size() == 4);
    const auto& net = map.at("fields")[0];
    CHECK(net.at("key") == "net");
    CHECK(net.at("type") == "integer");
    CHECK(net.at("required") == false);
    CHECK(net.at("default") == "0");
    CHECK(net.at("min") == 0);
    CHECK(net.at("max") == 255);
    CHECK(net.at("choices") == nlohmann::json::array());
    CHECK(map.at("fields")[1].at("key") == "station");
    CHECK(map.at("fields")[1].at("min") == 1);
    CHECK(map.at("fields")[1].at("max") == 254);
    CHECK(map.at("fields")[2].at("key") == "host");
    CHECK(map.at("fields")[3].at("key") == "port");
    CHECK(map.at("fields")[3].at("label") == "AUN UDP port");
    CHECK(subnet.at("item_format") == "{net}@{address}");
    REQUIRE(subnet.at("fields").size() == 2);
    CHECK(subnet.at("fields")[0].at("min") == 1);
    CHECK(subnet.at("fields")[1].at("key") == "address");
    CHECK(subnet.at("fields")[1].at("label") == "IPv4 subnet (a.b.c.0/24)");
    for (const auto& p : manifest.at("parameters")) {
        if (p.at("key") == "map" || p.at("key") == "subnet") continue;
        CHECK(p.at("fields") == nlohmann::json::array());
        CHECK(p.at("item_format") == "");
    }
}

TEST_CASE("start: a malformed AUN map item on the command line is refused before launch",
          "[integration][list-item]") {
    auto result = run_command(EXECUTABLE + " start --aun map=broken@entry --station 32"
                              " --port 0");
    CHECK(result.exit_code == 64);
    INFO(result.stderr_output);
    CHECK(result.stderr_output.find("map item 0") != std::string::npos);
    CHECK(result.stderr_output.find("{net}.{station}@{host}@{port}") != std::string::npos);
}

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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
    auto stdout_filepath = std::filesystem::temp_directory_path() / "beebium_test_stdout.txt";
    auto stderr_filepath = std::filesystem::temp_directory_path() / "beebium_test_stderr.txt";

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
class TempDirectory {
public:
    TempDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("beebium_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

private:
    std::filesystem::path path_;
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

// ============================================================================
// model-b-romram no longer auto-provisions DFS
// ============================================================================

TEST_CASE("model-b-romram boots without an auto-loaded DFS ROM",
          "[integration][romram][sideways]") {
    const std::string romram = find_executable("beebium-model-b-romram").string();
    auto output_filepath = std::filesystem::temp_directory_path() / "beebium_romram_bare.png";

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
    auto output_filepath =
        std::filesystem::temp_directory_path() / "beebium_gen_disc.preset.beebium";

    auto create_result = run_command(
        EXECUTABLE + " create-preset --name \"Gen Disc\" --release-date 1982"
        " --fdc acorn-1770 --sideways 14:rom:acorn-dfs_2_26.rom"
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
    auto screenshot_filepath =
        std::filesystem::temp_directory_path() / "beebium_gen_disc.png";
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
            std::filesystem::temp_directory_path() / ("beebium_shipped_" + c.preset_id + ".png");
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
    const std::string path =
        disc_path("games/Superior_Software/Repton_FSD0080_1.hfe");
    if (!std::filesystem::exists(path)) {
        SKIP("test HFE image not available: " + path);
    }
    auto result = run_command(EXECUTABLE + " describe-disc-image \"" + path + "\"");
    REQUIRE(result.exit_code == 0);
    REQUIRE(result.stdout_output.find("\"recognised\": true") != std::string::npos);
    REQUIRE(result.stdout_output.find("\"format\": \"HFE") != std::string::npos);
}

TEST_CASE("describe-disc-image reports a non-disc file as not recognised, with a reason",
          "[integration][disc][describe-disc-image]") {
    // A file that is plainly not a disc image. Exit is still 0 -- the file
    // was readable; the verdict is in the JSON, not the exit code.
    auto tmp = std::filesystem::temp_directory_path() / "beebium_not_a_disc.bin";
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

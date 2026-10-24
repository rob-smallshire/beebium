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

// Blank disc templates (#25): listing a directory of them, copying one into
// place, finding the shipped directory, and the list-blank-discs and
// create-disc subcommands.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/disc/BlankDiscs.hpp"
#include "beebium/server/BlankDiscSubcommands.hpp"
#include "beebium/server/SystemDirectories.hpp"
#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using beebium::test::ScopedEnv;
using beebium::test::ScopedTempDir;
using namespace beebium::server;

namespace {

void write_bytes(const fs::path& filepath, const std::vector<char>& bytes) {
    std::ofstream out(filepath, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::vector<char> read_bytes(const fs::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Distinct bytes per template, so a copy of the wrong one shows.
std::vector<char> pattern(std::size_t size, char seed) {
    std::vector<char> bytes(size);
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<char>(seed + i * 7);
    return bytes;
}

// A templates directory holding two floppies and two files that are not
// templates: a hidden file and one no disc format loads.
struct TemplatesDir {
    ScopedTempDir dir{"beebium-blank-discs-"};
    TemplatesDir() {
        write_bytes(dir / "Zed 80T.ssd", pattern(512, 3));
        write_bytes(dir / "Acorn ADFS S 160K.ads", pattern(1024, 5));
        write_bytes(dir / ".DS_Store", pattern(16, 1));
        write_bytes(dir / "README.txt", pattern(16, 2));
    }
};

std::vector<char*> argv_of(std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    return argv;
}

// Run a subcommand with the given arguments after its name, capturing stdout.
template <typename Command>
int run(std::vector<std::string> args, OutputFormat format, std::string& output) {
    args.insert(args.begin(), {"beebium", std::string(Command{}.name())});
    auto argv = argv_of(args);
    GlobalConfig global;
    global.subcommand_name = std::string(Command{}.name());
    global.subcommand_argv_start = 2;
    global.output_format = format;
    std::ostringstream capture;
    auto* previous = std::cout.rdbuf(capture.rdbuf());
    int code = Command{}.invoke(static_cast<int>(args.size()), argv.data(), global);
    std::cout.rdbuf(previous);
    output = capture.str();
    return code;
}

using ListCommand = ListBlankDiscsSubcommand<beebium::ModelB>;
using CreateCommand = CreateDiscSubcommand<beebium::ModelB>;

}  // namespace

TEST_CASE("list_blank_discs gives the templates in file name order", "[blank-discs]") {
    TemplatesDir templates;
    auto discs = beebium::list_blank_discs(templates.dir.path());

    REQUIRE(discs.size() == 2);
    CHECK(discs[0].label == "Acorn ADFS S 160K");
    CHECK(discs[0].filename == "Acorn ADFS S 160K.ads");
    CHECK(discs[0].kind == "floppy");
    CHECK(discs[0].size_bytes == 1024);
    CHECK(discs[1].label == "Zed 80T");
    CHECK(discs[1].size_bytes == 512);
}

TEST_CASE("list_blank_discs gives nothing without a directory", "[blank-discs]") {
    CHECK(beebium::list_blank_discs(std::nullopt).empty());
    CHECK(beebium::list_blank_discs(fs::path("/no/such/beebium/dir")).empty());
}

TEST_CASE("the committed templates list in file name order", "[blank-discs]") {
    auto discs = beebium::list_blank_discs(fs::path(BEEBIUM_BLANK_DISCS_DIR));

    std::vector<std::pair<std::string, std::uintmax_t>> listed;
    for (const auto& disc : discs) listed.emplace_back(disc.filename, disc.size_bytes);
    CHECK(listed == std::vector<std::pair<std::string, std::uintmax_t>>{
                        {"Acorn ADFS L 640K.adl", 655360},
                        {"Acorn ADFS M 320K.adm", 327680},
                        {"Acorn ADFS S 160K.ads", 163840},
                        {"Acorn DFS 40T DS 200K.dsd", 204800},
                        {"Acorn DFS 40T SS 100K.ssd", 102400},
                        {"Acorn DFS 80T DS 400K.dsd", 409600},
                        {"Acorn DFS 80T SS 200K.ssd", 204800},
                    });
    // Each is a disc the emulator loads.
    for (const auto& disc : discs) {
        INFO(disc.filename);
        CHECK(beebium::load_disc_from_url_or_filepath(disc.filepath.string()));
    }
}

TEST_CASE("create_blank_disc copies the template byte for byte", "[blank-discs]") {
    TemplatesDir templates;
    ScopedTempDir out{"beebium-blank-disc-out-"};
    auto destination = out / "new.ads";

    auto result = beebium::create_blank_disc(templates.dir.path(), "Acorn ADFS S 160K",
                                             destination, false);

    REQUIRE(result);
    CHECK(result.created_filepath == fs::absolute(destination));
    CHECK(read_bytes(destination) == pattern(1024, 5));
}

TEST_CASE("create_blank_disc makes a writable copy of a read-only template", "[blank-discs]") {
    TemplatesDir templates;
    fs::permissions(templates.dir / "Zed 80T.ssd", fs::perms::owner_read | fs::perms::group_read |
                                                       fs::perms::others_read);
    ScopedTempDir out{"beebium-blank-disc-out-"};
    auto destination = out / "new.ssd";

    REQUIRE(beebium::create_blank_disc(templates.dir.path(), "Zed 80T", destination, false));
    CHECK((fs::status(destination).permissions() & fs::perms::owner_write) != fs::perms::none);
}

TEST_CASE("create_blank_disc refuses an existing destination without overwrite",
          "[blank-discs]") {
    TemplatesDir templates;
    ScopedTempDir out{"beebium-blank-disc-out-"};
    auto destination = out / "existing.ssd";
    write_bytes(destination, {'k', 'e', 'e', 'p'});

    auto refused = beebium::create_blank_disc(templates.dir.path(), "Zed 80T", destination, false);
    CHECK_FALSE(refused);
    CHECK(refused.kind == beebium::CreateBlankDiscErrorKind::DestinationExists);
    CHECK(refused.error == "destination already exists: " + destination.string());
    CHECK(read_bytes(destination) == std::vector<char>{'k', 'e', 'e', 'p'});

    auto replaced = beebium::create_blank_disc(templates.dir.path(), "Zed 80T", destination, true);
    REQUIRE(replaced);
    CHECK(read_bytes(destination) == pattern(512, 3));
}

TEST_CASE("create_blank_disc refuses a directory as the destination, even with overwrite",
          "[blank-discs]") {
    TemplatesDir templates;
    ScopedTempDir out{"beebium-blank-disc-out-"};

    auto result = beebium::create_blank_disc(templates.dir.path(), "Zed 80T", out.path(), true);
    CHECK_FALSE(result);
    CHECK(result.kind == beebium::CreateBlankDiscErrorKind::DestinationExists);
    CHECK(fs::is_directory(out.path()));
}

TEST_CASE("create_blank_disc errors on an unknown label", "[blank-discs]") {
    TemplatesDir templates;
    ScopedTempDir out{"beebium-blank-disc-out-"};
    auto destination = out / "new.ssd";

    // A file that is not a template is no label either.
    for (const std::string label : {"No Such Disc", "README", ".DS_Store"}) {
        auto result = beebium::create_blank_disc(templates.dir.path(), label, destination, false);
        CHECK_FALSE(result);
        CHECK(result.kind == beebium::CreateBlankDiscErrorKind::UnknownLabel);
        CHECK(result.error == "no blank disc named '" + label + "'");
    }
    CHECK_FALSE(fs::exists(destination));
}

TEST_CASE("create_blank_disc reports a destination it cannot write", "[blank-discs]") {
    TemplatesDir templates;
    ScopedTempDir out{"beebium-blank-disc-out-"};

    auto result = beebium::create_blank_disc(templates.dir.path(), "Zed 80T",
                                             out / "missing" / "new.ssd", false);
    CHECK_FALSE(result);
    CHECK(result.kind == beebium::CreateBlankDiscErrorKind::WriteFailed);
}

TEST_CASE("the blank discs directory is found under BEEBIUM_SERVERS_DIRPATH", "[blank-discs]") {
    ScopedTempDir servers{"beebium-servers-"};
    fs::create_directory(servers / "blank-discs");
    ScopedEnv env("BEEBIUM_SERVERS_DIRPATH", servers.path().string());

    auto dirpath = find_blank_discs_dirpath();
    REQUIRE(dirpath);
    CHECK(*dirpath == servers / "blank-discs");
}

TEST_CASE("list-blank-discs lists the templates as TSV", "[blank-discs][cli]") {
    TemplatesDir templates;
    ScopedTempDir servers{"beebium-servers-"};
    fs::create_directory_symlink(templates.dir.path(), servers / "blank-discs");
    ScopedEnv env("BEEBIUM_SERVERS_DIRPATH", servers.path().string());

    std::string output;
    REQUIRE(run<ListCommand>({}, OutputFormat::Tsv, output) == ExitCode::OK);
    CHECK(output ==
          "label\tfilename\tkind\tsize_bytes\n"
          "Acorn ADFS S 160K\tAcorn ADFS S 160K.ads\tfloppy\t1024\n"
          "Zed 80T\tZed 80T.ssd\tfloppy\t512\n");
}

TEST_CASE("create-disc copies a template and prints the new disc's path", "[blank-discs][cli]") {
    TemplatesDir templates;
    ScopedTempDir servers{"beebium-servers-"};
    fs::create_directory_symlink(templates.dir.path(), servers / "blank-discs");
    ScopedEnv env("BEEBIUM_SERVERS_DIRPATH", servers.path().string());
    ScopedTempDir out{"beebium-blank-disc-out-"};
    auto destination = (out / "new.ssd").string();

    std::string output;
    REQUIRE(run<CreateCommand>({"Zed 80T", destination}, OutputFormat::Tsv, output) ==
            ExitCode::OK);
    CHECK(output == fs::absolute(destination).string() + "\n");
    CHECK(read_bytes(destination) == pattern(512, 3));

    // Again, without and then with --overwrite.
    CHECK(run<CreateCommand>({"Zed 80T", destination}, OutputFormat::Tsv, output) ==
          ExitCode::CANTCREAT);
    CHECK(run<CreateCommand>({"Acorn ADFS S 160K", destination, "--overwrite"}, OutputFormat::Tsv,
                             output) == ExitCode::OK);
    CHECK(read_bytes(destination) == pattern(1024, 5));
}

TEST_CASE("create-disc rejects an unknown label and missing arguments", "[blank-discs][cli]") {
    TemplatesDir templates;
    ScopedTempDir servers{"beebium-servers-"};
    fs::create_directory_symlink(templates.dir.path(), servers / "blank-discs");
    ScopedEnv env("BEEBIUM_SERVERS_DIRPATH", servers.path().string());
    ScopedTempDir out{"beebium-blank-disc-out-"};

    std::string output;
    CHECK(run<CreateCommand>({"No Such Disc", (out / "a.ssd").string()}, OutputFormat::Tsv,
                             output) == ExitCode::NOINPUT);
    CHECK(run<CreateCommand>({"Zed 80T"}, OutputFormat::Tsv, output) == ExitCode::USAGE);
    CHECK(run<CreateCommand>({"Zed 80T", "a", "b"}, OutputFormat::Tsv, output) ==
          ExitCode::USAGE);
    CHECK(run<CreateCommand>({"Zed 80T", "a", "--bogus"}, OutputFormat::Tsv, output) ==
          ExitCode::USAGE);
}

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

// test_machine_document.cpp
// Tests for the files of a machine directory the core defines
// (MachineDocument.hpp): the identity record, the document file, the folder
// metadata, and how a machine directory is recognised.

#include <beebium/server/MachineDocument.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "test_temp_paths.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>

using namespace beebium::server;
namespace fs = std::filesystem;

namespace {

std::string read_bytes(const fs::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

MachineRecord a_record() {
    return {"12345678-1234-1234-1234-123456789abc", "Elite Server {station}", "model-b",
            "2026-10-17T14:30:00Z", "2026-10-17T15:00:00Z", "model-b-l3fs-aun"};
}

}  // namespace

TEST_CASE("machine.json: a record written is read back", "[machine_document]") {
    beebium::test::ScopedTempDir dir("beebium_machine_doc_");
    write_machine_record(dir.path(), a_record());
    std::string error;
    const auto record = read_machine_record(dir.path(), error);
    REQUIRE(record.has_value());
    CHECK(record->uuid == "12345678-1234-1234-1234-123456789abc");
    CHECK(record->name == "Elite Server {station}");
    CHECK(record->model == "model-b");
    CHECK(record->created == "2026-10-17T14:30:00Z");
    CHECK(record->last_used == "2026-10-17T15:00:00Z");
    CHECK(record->source_preset == "model-b-l3fs-aun");
    CHECK(read_bytes(dir / "machine.json").rfind("{\n  \"format\": \"beebium-machine\",", 0) == 0);

    auto no_source = a_record();
    no_source.source_preset.clear();
    write_machine_record(dir.path(), no_source);
    CHECK(read_bytes(dir / "machine.json").find("source_preset") == std::string::npos);
}

TEST_CASE("machine.json: anything else is not a record", "[machine_document]") {
    beebium::test::ScopedTempDir dir("beebium_machine_doc_");
    std::string error;
    CHECK_FALSE(read_machine_record(dir.path(), error).has_value());
    CHECK_THAT(error, Catch::Matchers::StartsWith("no machine.json in "));

    std::ofstream(dir / "machine.json") << R"({"format": "something-else", "uuid": "x"})";
    CHECK_FALSE(read_machine_record(dir.path(), error).has_value());
    CHECK_THAT(error, Catch::Matchers::EndsWith("is not a machine record"));

    std::ofstream(dir / "machine.json")
        << R"({"format": "beebium-machine", "version": 99, "uuid": "x"})";
    CHECK_FALSE(read_machine_record(dir.path(), error).has_value());
    CHECK_THAT(error, Catch::Matchers::EndsWith("is from a later version of Beebium"));
}

TEST_CASE("machine directory: known by machine.json, named by itself or its document file",
          "[machine_document]") {
    beebium::test::ScopedTempDir root("beebium_machine_doc_");
    const auto machine = root / "Elite Server.beebium-machine";
    fs::create_directories(machine);
    CHECK_FALSE(is_machine_directory(machine));
    write_machine_record(machine, a_record());
    write_machine_document_files(machine, a_record(), "Elite Server");
    CHECK(is_machine_directory(machine));

    CHECK(machine_dirpath_named(machine) == machine);
    CHECK(machine_dirpath_named(machine / "Elite Server.beebium-machine") == machine);
    // A renamed or suffix-stripped directory is still one.
    fs::rename(machine, root / "renamed");
    CHECK(machine_dirpath_named(root / "renamed") == root / "renamed");
    // Anything else is not.
    CHECK_FALSE(machine_dirpath_named(root.path()).has_value());
    CHECK_FALSE(machine_dirpath_named(root / "missing").has_value());
}

TEST_CASE("a machine directory named without the suffix has a suffixed document file",
          "[machine_document]") {
    beebium::test::ScopedTempDir root("beebium_machine_doc_");
    const auto machine = root / "Elite Server";
    fs::create_directories(machine);
    CHECK(machine_document_filepath(machine) == machine / "Elite Server.beebium-machine");
    CHECK(machine_document_filepath(root / "Elite.beebium-machine") ==
          root / "Elite.beebium-machine" / "Elite.beebium-machine");

    write_machine_document_files(machine, a_record(), "Elite Server");
    CHECK(fs::is_regular_file(machine / "Elite Server.beebium-machine"));
    CHECK_FALSE(fs::exists(machine / "Elite Server"));  // no suffix-less document
}

TEST_CASE("the document file, desktop.ini and .directory present the machine",
          "[machine_document]") {
    beebium::test::ScopedTempDir root("beebium_machine_doc_");
    const auto machine = root / "Elite Server.beebium-machine";
    fs::create_directories(machine);
    std::ofstream(machine / "lock") << "1234";
    std::ofstream(machine / "Old Name.beebium-machine") << "{}";
    // "Elite " and an E-acute in UTF-8: a name beyond ASCII.
    const std::string display_name = "Elite \xC3\x89";

    write_machine_document_files(machine, a_record(), display_name);

    CHECK(read_bytes(machine / "Elite Server.beebium-machine") ==
          "{\n  \"format\": \"beebium-machine\",\n  \"version\": 1,\n"
          "  \"uuid\": \"12345678-1234-1234-1234-123456789abc\"\n}\n");
    CHECK_FALSE(fs::exists(machine / "Old Name.beebium-machine"));
    CHECK(read_bytes(machine / "lock") == "1234");
    CHECK(read_bytes(machine / ".directory") ==
          "[Desktop Entry]\nIcon=beebium-machine\nName=" + display_name + "\n");
    // UTF-16LE with a byte-order mark, ending "...Elite <E-acute>\r\n".
    const auto ini = read_bytes(machine / "desktop.ini");
    REQUIRE(ini.size() > 2);
    CHECK(ini.substr(0, 2) == std::string("\xFF\xFE", 2));
    CHECK(ini.substr(ini.size() - 6) == std::string("\xC9\x00\r\x00\n\x00", 6));

    // Written again (as on every Save), over the files the first time made.
    write_machine_document_files(machine, a_record(), "Second");
    CHECK(read_bytes(machine / ".directory").find("Name=Second") != std::string::npos);
#ifdef _WIN32
    const auto ini_attributes = ::GetFileAttributesW((machine / "desktop.ini").c_str());
    CHECK((ini_attributes & FILE_ATTRIBUTE_HIDDEN) != 0);
    CHECK((ini_attributes & FILE_ATTRIBUTE_SYSTEM) != 0);
    CHECK((::GetFileAttributesW(machine.c_str()) & FILE_ATTRIBUTE_READONLY) != 0);
    // The directory's read-only attribute does not stop its files changing.
    std::ofstream(machine / "game.ssd") << "disc";
    CHECK(fs::exists(machine / "game.ssd"));
#endif
}

TEST_CASE("the core's own entries are told from the machine's media", "[machine_document]") {
    for (const char* name : {"machine.json", "preset.json", "thumbnail.png", "nvram",
                             "extensions", "lock", "desktop.ini", ".directory",
                             "Elite Server.beebium-machine"}) {
        INFO(name);
        CHECK(machine_layout::is_core_entry(name));
    }
    for (const char* name : {"game.ssd", "l3fs-v1_26b.dat", "discs", "notes.txt"}) {
        INFO(name);
        CHECK_FALSE(machine_layout::is_core_entry(name));
    }
}

TEST_CASE("iso8601_utc_now: the time to the second, in UTC", "[machine_document]") {
    CHECK(std::regex_match(iso8601_utc_now(),
                           std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)")));
}

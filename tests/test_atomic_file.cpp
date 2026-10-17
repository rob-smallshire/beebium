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

// test_atomic_file.cpp
// Tests for replacing a file atomically (AtomicFile.hpp) and for the preset
// writer built on it (PresetWriter.hpp).

#include <beebium/server/AtomicFile.hpp>
#include <beebium/server/PresetWriter.hpp>
#include <catch2/catch_test_macros.hpp>

#include "test_temp_paths.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace beebium::server;
namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::size_t entry_count(const fs::path& dirpath) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator(dirpath)) ++count;
    return count;
}

}  // namespace

TEST_CASE("write_text_atomically: creates a file, then replaces it, byte for byte",
          "[atomic_file]") {
    beebium::test::ScopedTempDir dir("beebium_atomic_");
    write_text_atomically(dir / "f.txt", "first\r\n");
    CHECK(read_file(dir / "f.txt") == "first\r\n");
    write_text_atomically(dir / "f.txt", "second");
    CHECK(read_file(dir / "f.txt") == "second");
    CHECK(entry_count(dir.path()) == 1);
}

TEST_CASE("write_file_atomically: a write that fails part-way leaves the old file and no "
          "temporary",
          "[atomic_file]") {
    beebium::test::ScopedTempDir dir("beebium_atomic_");
    write_text_atomically(dir / "f.txt", "OLD");

    CHECK_THROWS_AS(write_file_atomically(dir / "f.txt",
                                          [](const fs::path& temp_filepath) {
                                              std::ofstream(temp_filepath) << "HALF";
                                              throw std::runtime_error("disc full");
                                          }),
                    std::runtime_error);

    CHECK(read_file(dir / "f.txt") == "OLD");
    CHECK(entry_count(dir.path()) == 1);
}

TEST_CASE("write_file_atomically: a read-only file is replaced by a writable one",
          "[atomic_file]") {
    beebium::test::ScopedTempDir dir("beebium_atomic_");
    write_text_atomically(dir / "f.txt", "OLD");
    fs::permissions(dir / "f.txt", fs::perms::owner_read);

    write_text_atomically(dir / "f.txt", "NEW");

    CHECK(read_file(dir / "f.txt") == "NEW");
    CHECK((fs::status(dir / "f.txt").permissions() & fs::perms::owner_write) != fs::perms::none);
}

TEST_CASE("write_preset: writes the document, or says why it cannot", "[atomic_file][preset]") {
    beebium::test::ScopedTempDir dir("beebium_atomic_");
    CHECK(write_preset("{\"name\": \"P\"}\n", dir / "p.preset.beebium").empty());
    CHECK(read_file(dir / "p.preset.beebium") == "{\"name\": \"P\"}\n");

    const auto missing_dirpath = dir / "no-such-dir";
    const auto error = write_preset("{}", missing_dirpath / "p.preset.beebium");
    CHECK(error.rfind("cannot write preset file: " +
                          (missing_dirpath / "p.preset.beebium").string() + " (",
                      0) == 0);
    CHECK_FALSE(fs::exists(missing_dirpath));
}

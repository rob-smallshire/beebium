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

// test_zip_archive.cpp
// Tests for reading and writing zip archives (ZipArchive.hpp).

#include <beebium/zip/ZipArchive.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "test_temp_paths.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace beebium::zip;
namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

namespace {

// An archive written by another tool, Python's zipfile, with its members
// deflated and a directory entry:
//
//   import io, zipfile
//   buf = io.BytesIO()
//   with zipfile.ZipFile(buf, "w") as z:
//       for name, data in [("preset.json", b'{"model": "model-b"}\n'),
//                          ("discs/", b""), ("discs/game.ssd", b"\xe5" * 2048)]:
//           info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
//           info.compress_type = zipfile.ZIP_DEFLATED
//           info.create_system = 0
//           info.external_attr = 0
//           z.writestr(info, data)
const std::vector<std::uint8_t> kDeflatedByPython{
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x23, 0x09,
    0xfe, 0x85, 0x12, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x70, 0x72,
    0x65, 0x73, 0x65, 0x74, 0x2e, 0x6a, 0x73, 0x6f, 0x6e, 0xab, 0x56, 0xca, 0xcd, 0x4f, 0x49, 0xcd,
    0x51, 0xb2, 0x52, 0x80, 0x30, 0x74, 0x93, 0x94, 0x6a, 0xb9, 0x00, 0x50, 0x4b, 0x03, 0x04, 0x14,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x64, 0x69, 0x73, 0x63, 0x73, 0x2f, 0x03,
    0x00, 0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0xc9,
    0xda, 0xcd, 0xaa, 0x12, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x64,
    0x69, 0x73, 0x63, 0x73, 0x2f, 0x67, 0x61, 0x6d, 0x65, 0x2e, 0x73, 0x73, 0x64, 0x7b, 0xfa, 0x74,
    0x14, 0x8c, 0x82, 0x51, 0x30, 0x0a, 0x46, 0xc1, 0x28, 0x18, 0x05, 0x23, 0x0d, 0x00, 0x00, 0x50,
    0x4b, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x23,
    0x09, 0xfe, 0x85, 0x12, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x70, 0x72, 0x65,
    0x73, 0x65, 0x74, 0x2e, 0x6a, 0x73, 0x6f, 0x6e, 0x50, 0x4b, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x01, 0x3b, 0x00, 0x00, 0x00, 0x64, 0x69, 0x73, 0x63, 0x73, 0x2f, 0x50, 0x4b, 0x01, 0x02,
    0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0xc9, 0xda, 0xcd, 0xaa,
    0x12, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x61, 0x00, 0x00, 0x00, 0x64, 0x69, 0x73, 0x63, 0x73, 0x2f,
    0x67, 0x61, 0x6d, 0x65, 0x2e, 0x73, 0x73, 0x64, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x03, 0x00, 0xa9, 0x00, 0x00, 0x00, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::vector<std::uint8_t> bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> read_file(const fs::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

void write_file(const fs::path& filepath, const std::vector<std::uint8_t>& data) {
    std::ofstream(filepath, std::ios::binary)
        .write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

// An archive at `filepath` holding one member called `name`, which no
// writer of ours would write: written with `placeholder`, a valid name of
// the same length, then renamed in place (the checksum covers the data, not
// the name).
void write_archive_naming(const fs::path& filepath, const std::string& placeholder,
                          const std::string& name) {
    REQUIRE(placeholder.size() == name.size());
    write_zip(filepath, {{placeholder, bytes("x")}});
    auto data = read_file(filepath);
    auto at = data.begin();
    int renamed = 0;
    while ((at = std::search(at, data.end(), placeholder.begin(), placeholder.end())) !=
           data.end()) {
        std::copy(name.begin(), name.end(), at);
        ++renamed;
    }
    REQUIRE(renamed == 2);  // the local header and the central directory
    write_file(filepath, data);
}

}  // namespace

TEST_CASE("is_valid_member_name: relative paths within the archive only", "[zip]") {
    CHECK(is_valid_member_name("preset.json"));
    CHECK(is_valid_member_name("discs/game.ssd"));
    CHECK(is_valid_member_name("discs/"));
    CHECK(is_valid_member_name("a.b/..c"));
    CHECK_FALSE(is_valid_member_name(""));
    CHECK_FALSE(is_valid_member_name("/abs"));
    CHECK_FALSE(is_valid_member_name("../up"));
    CHECK_FALSE(is_valid_member_name("a/../b"));
    CHECK_FALSE(is_valid_member_name("a/./b"));
    CHECK_FALSE(is_valid_member_name("a//b"));
    CHECK_FALSE(is_valid_member_name("a\\b"));
    CHECK_FALSE(is_valid_member_name("C:x"));
    CHECK_FALSE(is_valid_member_name("/"));
}

TEST_CASE("write_zip and ZipReader: members round-trip, listed in name order", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    const auto big = std::vector<std::uint8_t>(100000, 0xe5);
    write_zip(dir / "a.zip", {{"thumbnail.png", bytes("PNG")},
                              {"discs/game.ssd", big},
                              {"preset.json", bytes("{}")},
                              {"empty", {}}});

    ZipReader reader(dir / "a.zip");
    CHECK(reader.member_names() ==
          std::vector<std::string>{"discs/game.ssd", "empty", "preset.json", "thumbnail.png"});
    CHECK(reader.contains("discs/game.ssd"));
    CHECK_FALSE(reader.contains("discs"));
    CHECK_FALSE(reader.contains("missing"));
    CHECK(reader.read("preset.json") == bytes("{}"));
    CHECK(reader.read("empty").empty());
    CHECK(reader.read("discs/game.ssd") == big);
    // Compressed: the archive is much smaller than what it holds.
    CHECK(fs::file_size(dir / "a.zip") < big.size() / 10);

    reader.extract("discs/game.ssd", dir / "game.ssd");
    CHECK(read_file(dir / "game.ssd") == big);
    CHECK_THROWS_WITH(reader.read("missing"), ContainsSubstring("no member missing"));
}

TEST_CASE("write_zip: the same members give byte-identical archives, in any order", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    write_zip(dir / "one.zip", {{"b", bytes("B")}, {"a/c", bytes("C")}});
    write_zip(dir / "two.zip", {{"a/c", bytes("C")}, {"b", bytes("B")}});
    CHECK(read_file(dir / "one.zip") == read_file(dir / "two.zip"));
}

TEST_CASE("write_zip: every member is dated 1980-01-01 12:00", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    write_zip(dir / "a.zip", {{"a", bytes("A")}, {"d/b", std::vector<std::uint8_t>(5000, 1)}});
    const auto data = read_file(dir / "a.zip");
    auto get16 = [&](std::size_t at) { return data.at(at) | (data.at(at + 1) << 8); };
    // Every local header and central directory entry: time 0x6000, date 0x0021.
    int local = 0;
    int central = 0;
    for (std::size_t at = 0; at + 4 <= data.size(); ++at) {
        if (get16(at) == 0x4b50 && get16(at + 2) == 0x0403) {
            CHECK(get16(at + 10) == 0x6000);
            CHECK(get16(at + 12) == 0x0021);
            ++local;
        } else if (get16(at) == 0x4b50 && get16(at + 2) == 0x0201) {
            CHECK(get16(at + 12) == 0x6000);
            CHECK(get16(at + 14) == 0x0021);
            ++central;
        }
    }
    CHECK(local == 2);
    CHECK(central == 2);
    // Still a sound archive.
    ZipReader reader(dir / "a.zip");
    CHECK(reader.read("d/b") == std::vector<std::uint8_t>(5000, 1));
}

TEST_CASE("write_zip: refuses a name that is not a member name, or two members alike", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    for (const char* name : {"../up", "/abs", "a\\b", "dir/", ""}) {
        INFO(name);
        CHECK_THROWS_AS(write_zip(dir / "bad.zip", {{name, bytes("x")}}), ZipError);
    }
    CHECK_THROWS_WITH(write_zip(dir / "dup.zip", {{"same", bytes("1")}, {"same", bytes("2")}}),
                      ContainsSubstring("two members are called same"));
}

TEST_CASE("ZipReader: reads an archive another tool deflated, leaving out directories", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    write_file(dir / "python.zip", kDeflatedByPython);

    ZipReader reader(dir / "python.zip");
    CHECK(reader.member_names() == std::vector<std::string>{"preset.json", "discs/game.ssd"});
    CHECK(reader.read("preset.json") == bytes("{\"model\": \"model-b\"}\n"));
    CHECK(reader.read("discs/game.ssd") == std::vector<std::uint8_t>(2048, 0xe5));
}

TEST_CASE("ZipReader: an archive naming a member outside itself is refused", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    const std::vector<std::pair<std::string, std::string>> cases{
        {"xx/evil.txt", "../evil.txt"},
        {"xabs.txt", "/abs.txt"},
        {"a_b.txt", "a\\b.txt"},
        {"Cqx.txt", "C:x.txt"},
        {"a/xb.txt", "a//b.txt"},
    };
    for (const auto& [placeholder, name] : cases) {
        INFO(name);
        write_archive_naming(dir / "bad.zip", placeholder, name);
        CHECK_THROWS_WITH(ZipReader(dir / "bad.zip"),
                          ContainsSubstring("member name '" + name +
                                            "' is not a relative path within the archive"));
    }
}

TEST_CASE("ZipReader: an archive naming two members alike is refused", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    write_zip(dir / "dup.zip", {{"same1", bytes("1")}, {"same2", bytes("2")}});
    auto data = read_file(dir / "dup.zip");
    const std::string from = "same2";
    for (auto at = data.begin();
         (at = std::search(at, data.end(), from.begin(), from.end())) != data.end();) {
        *(at + 4) = '1';
    }
    write_file(dir / "dup.zip", data);
    CHECK_THROWS_WITH(ZipReader(dir / "dup.zip"), ContainsSubstring("two members are called same1"));
}

TEST_CASE("ZipReader: a file that is not a zip archive is refused, naming it", "[zip]") {
    beebium::test::ScopedTempDir dir("beebium_zip_");
    write_file(dir / "not.zip", bytes("{\"model\": \"model-b\"}"));
    CHECK_THROWS_WITH(ZipReader(dir / "not.zip"),
                      ContainsSubstring((dir / "not.zip").string() + ": not a zip archive"));
    CHECK_THROWS_WITH(ZipReader(dir / "missing.zip"), ContainsSubstring(": cannot open"));
}

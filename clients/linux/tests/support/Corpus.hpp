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

// Access to the keyboard mapping conformance corpus
// (tests/assets/keyboard-mappings, docs/keyboard-mapping-files.md 6).

#ifndef BEEBIUM_LINUX_TESTS_CORPUS_HPP
#define BEEBIUM_LINUX_TESTS_CORPUS_HPP

#include "keyboard/MappingFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace beebium::linux_client::testing {

inline std::string corpus_text(const std::string& filename) {
    std::ifstream file(std::string(BEEBIUM_KEYBOARD_CORPUS_DIRPATH) + "/" + filename);
    REQUIRE(file.good());
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

inline MappingFile corpus_mapping(const std::string& filename) {
    auto result = parse_mapping_file(corpus_text(filename));
    if (auto* error = std::get_if<MappingFileError>(&result)) {
        FAIL(filename << ": " << error->reason);
    }
    return std::get<MappingFile>(std::move(result));
}

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_CORPUS_HPP

// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// RomCheck.hpp, checks a ROM against its facts file
// version 2.0.0, 20 November 2026.

// One check, shared by every test tied to a ROM (USER, 1 Nov 2026): the banner tests, the banner
// tool and the Executive tests. What a test expects of a ROM comes from the facts file named by
// its SHA-1, so a ROM with no file skips those tests rather than failing them.

#pragma once

#include <beebium/Sha1.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <vector>

using RomFacts = std::map<std::string, std::string>;

inline std::string sha1_text(const std::vector<uint8_t>& image) {
    std::string text;
    for (uint8_t byte : beebium::sha1(std::span<const uint8_t>(image))) {
        char pair[3];
        snprintf(pair, sizeof pair, "%02x", byte);
        text += pair;
    }
    return text;
}

// The ROM BEEBIUM_AES_ROM_FILEPATH names, else the one the plugin ships (AES_BUNDLED_ROM_FILEPATH,
// set by the build). Empty if neither can be read: rom_problem then says so.
inline std::vector<uint8_t> rom_from_env() {
    const char* path = getenv("BEEBIUM_AES_ROM_FILEPATH");
#ifdef AES_BUNDLED_ROM_FILEPATH
    if (!path) path = AES_BUNDLED_ROM_FILEPATH;
#endif
    if (!path) return {};
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

// Everything after the first = is the value, spaces included: the ROM's texts end in spaces.
inline RomFacts read_rom_facts(std::istream& file) {
    RomFacts facts;
    for (std::string line; std::getline(file, line);) {
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == '#' || equals == std::string::npos) continue;
        facts[line.substr(0, equals)] = line.substr(equals + 1);
    }
    return facts;
}

// AES_ROM_FACTS_DIR is set by the build, as Beebium's tests get BEEBIUM_ROM_DIR (SRC).
inline RomFacts rom_facts_for(const std::vector<uint8_t>& image) {
    std::ifstream file(std::string(AES_ROM_FACTS_DIR) + "/" + sha1_text(image) + ".txt");
    return read_rom_facts(file);
}

// Why tests tied to a ROM cannot run on this one; empty if they can.
inline std::string rom_problem(const std::vector<uint8_t>& image) {
    if (image.empty()) return "no ROM: BEEBIUM_AES_ROM_FILEPATH is not set, or names no file";
    if (rom_facts_for(image).empty()) return "no facts file for this ROM, SHA-1 " + sha1_text(image);
    return "";
}

// SRC, ROM bytes at banner_at for the words; the size is the setting ram; the line ends
// are SRC, Super1:192, 215-232 (notes/test-specs.md, 1.4). Not the source's or UTIL's wording.
inline std::string expected_banner_words(const std::vector<uint8_t>& image, int ram_mb) {
    std::string words;
    for (size_t at = std::stoul(rom_facts_for(image).at("banner_at"), nullptr, 0); image[at] != 0; at++) words += char(image[at]);
    return words + std::to_string(ram_mb * 1024) + "K";
}

inline std::string expected_banner(const std::vector<uint8_t>& image, int ram_mb) {
    return "\n\r" + expected_banner_words(image, ram_mb) + "\n\r\n\r";
}

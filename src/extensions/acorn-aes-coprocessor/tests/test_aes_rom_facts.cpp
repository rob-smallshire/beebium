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
// test_aes_rom_facts.cpp, tests the ROM facts file
// version 2.0.0, 20 November 2026.

// The ROM facts files (USER, 1 Nov 2026): every offset and expected text a
// test ties to one ROM comes from the file named by that ROM's SHA-1. The ROM decides here.

#include "ArmTest.hpp"
#include "RomCheck.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

static bool holds(const std::vector<uint8_t>& image, size_t at, const std::string& text) {
    return at + text.size() <= image.size() && std::equal(text.begin(), text.end(), image.begin() + at, [](char a, uint8_t b) { return uint8_t(a) == b; });
}

static size_t find_in(const std::vector<uint8_t>& image, const std::string& text) {
    const auto found = std::search(image.begin(), image.end(), text.begin(), text.end(), [](uint8_t a, char b) { return a == uint8_t(b); });
    return found == image.end() ? std::string::npos : size_t(found - image.begin());
}

TEST(facts_file_format, "A facts file: comments skipped, the first = divides, trailing spaces kept (stage outline 4c)") {
    std::istringstream file("# a comment\n\nplain=1\nspaced=two words \nsum=a=b\n");
    const RomFacts facts = read_rom_facts(file);
    CHECK_EQ(facts.size(), 3);
    CHECK_EQ(facts.count("plain") && facts.at("plain") == "1", true);
    CHECK_EQ(facts.count("spaced") && facts.at("spaced") == "two words ", true);
    CHECK_EQ(facts.count("sum") && facts.at("sum") == "a=b", true);
}

TEST(facts_no_rom_reason, "No ROM at all gives a plain reason, not a crash (stage outline 4c)") {
    CHECK_EQ(rom_problem({}).find("AES_ROM") != std::string::npos, true);
}

TEST(facts_unknown_rom_reason, "A ROM with no facts file gives a plain reason, naming its SHA-1 (stage outline 4c)") {
    const std::vector<uint8_t> other(0x4000, 0);
    const std::string why = rom_problem(other);
    CHECK_EQ(why.find(sha1_text(other)) != std::string::npos, true);
}

TEST(facts_agree_with_the_rom, "Every text. fact is in the ROM; the banner, prompt and Memory load are at their stated places (SRC, ROM bytes)") {
    const std::vector<uint8_t> image = rom_from_env();
    NEED_ROM(image);
    const RomFacts facts = rom_facts_for(image);
    int texts = 0;
    for (const auto& [key, value] : facts) {
        if (key.rfind("text.", 0) != 0) continue;
        texts++;
        if (find_in(image, value) == std::string::npos) printf("  not in the ROM: %s\n", key.c_str());
        CHECK_EQ(find_in(image, value) != std::string::npos, true);
    }
    CHECK_EQ(texts > 0, true);
    CHECK_EQ(holds(image, std::stoul(facts.at("banner_at"), nullptr, 0), facts.at("text.banner")), true);
    std::string prompt;
    std::istringstream bytes(facts.at("prompt_bytes"));
    for (int byte; bytes >> byte;) prompt += char(byte);
    CHECK_EQ(holds(image, std::stoul(facts.at("prompt_at"), nullptr, 0), prompt), true);
    const uint32_t word = uint32_t(std::stoul(facts.at("memory_load_word"), nullptr, 0));
    const std::string load{char(word), char(word >> 8), char(word >> 16), char(word >> 24)};
    const size_t after = find_in(image, facts.at("text.memory_anchor"));
    const auto rest = image.begin() + std::min(after, image.size());
    CHECK_EQ(std::search(rest, image.end(), load.begin(), load.end(), [](uint8_t a, char b) { return a == uint8_t(b); }) != image.end(), true);
}

TEST(facts_said_lines_labelled, "Every said. line has its own label just above it: SRC and a source line, and each quoted part is in the ROM and in the line (a gap found in an earlier review: unlabelled said. lines)") {
    const std::vector<uint8_t> image = rom_from_env();
    NEED_ROM(image);
    std::ifstream file(std::string(AES_ROM_FACTS_DIR) + "/" + sha1_text(image) + ".txt");
    std::string line, above;
    int said = 0;
    while (std::getline(file, line)) {
        if (line.rfind("said.", 0) == 0) {
            said++;
            const std::string value = line.substr(line.find('=') + 1);
            const bool labelled = above.rfind("# SRC", 0) == 0 &&
                                  (above.find("Super1:") != std::string::npos || above.find("Middle:") != std::string::npos);
            if (!labelled) printf("  no label above: %s\n", line.c_str());
            CHECK_EQ(labelled, true);
            int parts = 0;
            for (size_t open = above.find('"'); open != std::string::npos; open = above.find('"', open + 1)) {
                const size_t close = above.find('"', open + 1);
                if (close == std::string::npos) break;
                const std::string part = above.substr(open + 1, close - open - 1);
                parts++;
                if (find_in(image, part) == std::string::npos) printf("  not in the ROM: %s\n", part.c_str());
                CHECK_EQ(find_in(image, part) != std::string::npos, true);
                CHECK_EQ(value.find(part) != std::string::npos, true);
                open = close;
            }
            CHECK_EQ(parts > 0, true);
        }
        if (!line.empty()) above = line;
    }
    CHECK_EQ(said, 6);
}

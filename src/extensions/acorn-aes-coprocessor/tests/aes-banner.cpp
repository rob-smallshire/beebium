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
// aes-banner.cpp, prints the expected banner, for the Python tests
// version 2.0.0, 20 November 2026.

// Prints the banner line expected for a ROM and a RAM size, or refuses. The server test uses
// it, so that there is only one check (RomCheck.hpp). Use: aes-banner <rom file> <MB>

#include "RomCheck.hpp"

#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("usage: aes-banner <rom file> <MB>\n");
        return 2;
    }
    std::ifstream file(argv[1], std::ios::binary);
    const std::vector<uint8_t> image{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const std::string why = rom_problem(image);
    if (!why.empty()) {
        printf("REFUSED: %s\n", why.c_str());
        return 1;
    }
    printf("%s\n", expected_banner_words(image, atoi(argv[2])).c_str());
    return 0;
}

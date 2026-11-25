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
// test_aes_banner.cpp, tests the banner from the ROM
// version 2.0.0, 20 November 2026.

// The banner through the Tube chip, on the real ROM, tied to it by SHA-1 (USER, 1 Nov 2026).
// The host here is two lines of polling; the full machine's banner is in test_server.py.

#include "../beebium/AesTube.hpp"
#include "ArmTest.hpp"
#include "RomCheck.hpp"

#include <beebium/tube/TubeUla.hpp>

#include <string>

// DOC, AN004 p.11: host R1STAT bit 7 is data ready, R1 data is at offset 1.
static std::string text_through_r1(const std::vector<uint8_t>& rom, int ram_mb) {
    AesOptions o;
    o.ram = ram_mb;
    beebium::TubeUla ula;
    AesTube tube(o, ula);
    tube.set_rom(rom);
    std::string text;
    for (uint64_t now = 0; now < 2000000 && (text.empty() || text.back() != 0); now += 8) {
        tube.run_until(now);
        while (ula.host_read(0) & 0x80) text += char(ula.host_read(1));
    }
    return text;
}

static void check_banner(int ram_mb) {
    const std::vector<uint8_t> rom = rom_from_env();
    NEED_ROM(rom);
    const std::string expected = expected_banner(rom, ram_mb) + std::string(1, '\0');
    const std::string text = text_through_r1(rom, ram_mb);
    if (text != expected) printf("  got [%s]\n", text.c_str());
    CHECK_EQ(text == expected, true);
}

TEST(banner_4096k, "Real ROM, ram 4: the banner from the ROM bytes at &1938 with 4096K, then one zero byte, through R1 of the chip (USER, 1 Nov 2026; ROM bytes; SRC, Super1:192, 215-246; DOC, AN p.28)") {
    check_banner(4);
}

TEST(banner_2048k, "Real ROM, ram 2: the same with 2048K (USER, 1 Nov 2026; ROM bytes; SRC, Super1:163-191)") {
    check_banner(2);
}

TEST(banner_refuses_other_rom, "The shared check turns away a ROM one byte different, and says which it has (USER, 1 Nov 2026)") {
    std::vector<uint8_t> rom = rom_from_env();
    NEED_ROM(rom);
    CHECK_EQ(rom_problem(rom).empty(), true);
    rom[std::stoul(rom_facts_for(rom).at("banner_at"), nullptr, 0)] ^= 1;
    CHECK_EQ(rom_problem(rom).find(sha1_text(rom)) != std::string::npos, true);
}

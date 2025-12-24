// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Test keyboard input through SystemViaPeripheral
//
// This test verifies that key presses set via SystemViaPeripheral::key_down()
// are correctly detected by the MOS keyboard scanning routine.

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace beebium;

namespace {

// Load a ROM file into a vector
std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

// Check if ROM files exist
bool roms_available() {
#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "OS12.ROM") &&
           std::filesystem::exists(rom_dir / "BASIC2.ROM");
#else
    return false;
#endif
}

// Boot the machine to BASIC prompt (run ~2.5 million cycles)
void boot_to_basic(ModelB& machine) {
    constexpr uint64_t boot_cycles = 2'500'000;
    machine.run(boot_cycles);
}

// Read a character from Mode 7 screen memory
// Mode 7 screen is at $7C00, 40x25 characters
char read_screen_char(ModelB& machine, int row, int col) {
    constexpr uint16_t screen_base = 0x7C00;
    uint16_t addr = screen_base + row * 40 + col;
    uint8_t byte = machine.read(addr);
    // Mode 7 characters above 0x20 are printable
    return (byte >= 0x20 && byte < 0x7F) ? static_cast<char>(byte) : '.';
}

// Find a string in screen memory (Mode 7)
bool screen_contains(ModelB& machine, const std::string& text) {
    for (int row = 0; row < 25; ++row) {
        std::string line;
        for (int col = 0; col < 40; ++col) {
            line += read_screen_char(machine, row, col);
        }
        if (line.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

TEST_CASE("Keyboard matrix scanning returns correct bit pattern", "[keyboard]") {
    // This test doesn't need ROMs - it just tests the peripheral's matrix scanning

    ModelB machine;
    auto& peripheral = machine.state().memory.system_via_peripheral;

    // Press 'H' at row 5, column 4
    peripheral.key_down(5, 4);

    // The BBC scans the keyboard by writing a key number to Port A and
    // checking bit 7 of the response.
    //
    // For key at row R, column C: key_number = (R << 4) | C
    // For H (row 5, col 4): key_number = 0x54
    //
    // If the key is pressed, bit 7 should be SET in the response.

    // Simulate the MOS keyboard scan by calling update_port_a
    uint8_t key_number = (5 << 4) | 4;  // 0x54 for H
    uint8_t result = peripheral.update_port_a(key_number, 0xFF);

    // Bit 7 should be set when key is pressed
    CHECK((result & 0x80) != 0);
    CHECK(result == (key_number | 0x80));

    // Now release the key
    peripheral.key_up(5, 4);

    // Scan again - bit 7 should be clear
    result = peripheral.update_port_a(key_number, 0xFF);
    CHECK((result & 0x80) == 0);
    CHECK(result == key_number);
}

TEST_CASE("Keyboard input appears in screen memory after boot", "[keyboard][boot]") {
    if (!roms_available()) {
        SKIP("ROMs not available");
    }

#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);

    ModelB machine;

    // Load ROMs
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
    std::copy(basic.begin(), basic.end(), machine.state().memory.basic_rom.data());

    machine.reset();

    // Boot to BASIC prompt
    std::cout << "Booting to BASIC...\n";
    boot_to_basic(machine);

    // Verify we've booted - screen should contain "BBC Computer"
    REQUIRE(screen_contains(machine, "BBC Computer"));

    auto& peripheral = machine.state().memory.system_via_peripheral;

    // Press 'H' (row 5, col 4)
    peripheral.key_down(5, 4);

    // Run for enough cycles for MOS to scan the keyboard and process the key
    machine.run(200'000);

    // Verify key is in matrix
    CHECK(peripheral.is_key_pressed(5, 4));

    // Release the key
    peripheral.key_up(5, 4);

    // Run more cycles to let MOS finish processing
    machine.run(100'000);

    // Check if 'H' appears on the screen (BBC BASIC echoes uppercase by default)
    INFO("Looking for 'H' on screen after keypress");
    bool found = screen_contains(machine, "H");
    CHECK(found);
#endif
}

TEST_CASE("Verify key is detected by MOS keyboard scan", "[keyboard][boot]") {
    if (!roms_available()) {
        SKIP("ROMs not available");
    }

#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);

    ModelB machine;

    // Load ROMs
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    std::copy(mos.begin(), mos.end(), machine.state().memory.mos_rom.data());
    std::copy(basic.begin(), basic.end(), machine.state().memory.basic_rom.data());

    machine.reset();

    // Boot to BASIC
    boot_to_basic(machine);

    auto& peripheral = machine.state().memory.system_via_peripheral;

    // Verify keyboard scan is working by checking that:
    // 1. When no key is pressed, all scans return bit 7 = 0
    // 2. When a key is pressed, scanning that key returns bit 7 = 1

    // First, verify no keys pressed
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 10; ++col) {
            uint8_t key_num = (row << 4) | col;
            uint8_t result = peripheral.update_port_a(key_num, 0xFF);
            if ((result & 0x80) != 0) {
                FAIL("Unexpected key press detected at row=" << row << " col=" << col);
            }
        }
    }

    std::cout << "No spurious keys detected.\n";

    // Now press 'A' (row 4, col 1)
    peripheral.key_down(4, 1);

    // Verify only 'A' reads as pressed
    uint8_t a_key_num = (4 << 4) | 1;  // 0x41
    uint8_t result = peripheral.update_port_a(a_key_num, 0xFF);
    CHECK((result & 0x80) != 0);

    // Verify other keys still not pressed
    uint8_t h_key_num = (5 << 4) | 4;  // 0x54
    result = peripheral.update_port_a(h_key_num, 0xFF);
    CHECK((result & 0x80) == 0);

    std::cout << "Key detection working correctly.\n";
#endif
}

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

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace beebium;

namespace {
std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) throw std::runtime_error("Failed to open ROM: " + filepath.string());
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}
}

TEST_CASE("Trace clearRAM loop execution", "[debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dir / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    // First verify ROM is loaded correctly
    std::cout << "=== Verifying ROM at clearRAM loop ===" << std::endl;
    for (uint16_t addr = 0xD9ED; addr < 0xD9FD; ++addr) {
        std::cout << std::hex << std::setw(4) << std::setfill('0') << addr
                  << ": " << std::setw(2) << static_cast<int>(machine.read(addr))
                  << std::endl;
    }

    machine.reset();

    // Complete reset
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Run until we reach clearRAM loop entry ($D9EE is STA ($00),Y)
    // NOTE: At IsAboutToExecute, pc.w is ONE PAST the opcode (pre-incremented by M6502_NextInstruction)
    // Use opcode_pc.w to get the actual instruction address
    std::cout << "\n=== Running to clearRAM loop ===" << std::endl;
    int steps = 0;
    while (machine.cpu().opcode_pc.w != 0xD9EE && steps < 100) {
        uint16_t opc = machine.cpu().opcode_pc.w;
        uint16_t pc = machine.cpu().pc.w;
        uint8_t opcode = machine.read(opc);
        std::cout << "Step " << std::dec << steps << ": opcode_pc=$" << std::hex << opc
                  << " pc=$" << pc
                  << " op=$" << std::setw(2) << static_cast<int>(opcode)
                  << " A=$" << std::setw(2) << static_cast<int>(machine.cpu().a)
                  << " X=$" << std::setw(2) << static_cast<int>(machine.cpu().x)
                  << " Y=$" << std::setw(2) << static_cast<int>(machine.cpu().y)
                  << std::endl;
        machine.step_instruction();
        steps++;
    }

    // Now trace 50 iterations of the inner loop
    std::cout << "\n=== Tracing clearRAM loop (50 instructions) ===" << std::endl;
    for (int iter = 0; iter < 50; ++iter) {
        if (!M6502_IsAboutToExecute(&machine.cpu())) {
            std::cout << "ERROR: Not at instruction start at iter " << iter << std::endl;
            break;
        }

        uint16_t opc = machine.cpu().opcode_pc.w;  // Actual instruction address
        uint16_t pc = machine.cpu().pc.w;           // One past opcode (pre-incremented)
        uint8_t opcode = machine.read(opc);
        uint8_t a = machine.cpu().a;
        uint8_t x = machine.cpu().x;
        uint8_t y = machine.cpu().y;
        uint8_t zp0 = machine.read(0x00);
        uint8_t zp1 = machine.read(0x01);

        std::cout << std::dec << std::setw(3) << iter << ": "
                  << "opc=$" << std::hex << std::setw(4) << std::setfill('0') << opc
                  << " pc=$" << std::setw(4) << pc
                  << " op=$" << std::setw(2) << static_cast<int>(opcode)
                  << " A=$" << std::setw(2) << static_cast<int>(a)
                  << " X=$" << std::setw(2) << static_cast<int>(x)
                  << " Y=$" << std::setw(2) << static_cast<int>(y)
                  << " [$00]=$" << std::setw(2) << static_cast<int>(zp0)
                  << " [$01]=$" << std::setw(2) << static_cast<int>(zp1)
                  << std::dec << std::endl;

        machine.step_instruction();

        // Check if we left the clearRAM region
        if (machine.cpu().pc.w >= 0xDA10) {
            std::cout << "Exited clearRAM region to $" << std::hex << machine.cpu().pc.w << std::endl;
            break;
        }
    }

    std::cout << "\n=== Final state ===" << std::endl;
    std::cout << "PC=$" << std::hex << machine.cpu().pc.w << std::endl;
    std::cout << "A=$" << std::hex << static_cast<int>(machine.cpu().a) << std::endl;
    std::cout << "[$00]=$" << std::setw(2) << static_cast<int>(machine.read(0x00)) << std::endl;
    std::cout << "[$01]=$" << std::setw(2) << static_cast<int>(machine.read(0x01)) << std::endl;

    REQUIRE(true);
}

TEST_CASE("Cycle-by-cycle STA ($00),Y execution", "[debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dir / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    machine.reset();

    // Complete reset
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Run to $D9EE
    while (machine.cpu().pc.w != 0xD9EE) {
        machine.step_instruction();
    }

    std::cout << "=== At STA ($00),Y ===" << std::endl;
    std::cout << "PC=$" << std::hex << machine.cpu().pc.w << std::endl;
    std::cout << "[$00]=$" << static_cast<int>(machine.read(0x00))
              << " [$01]=$" << static_cast<int>(machine.read(0x01))
              << " Y=$" << static_cast<int>(machine.cpu().y) << std::endl;

    // Target address should be: ([$00] | [$01]<<8) + Y = $0400 + 0 = $0400
    uint16_t target = machine.read(0x00) | (machine.read(0x01) << 8);
    target += machine.cpu().y;
    std::cout << "Expected write target: $" << std::hex << target << std::endl;
    std::cout << "A register (value to write): $" << static_cast<int>(machine.cpu().a) << std::endl;

    // Execute STA ($00),Y cycle by cycle
    std::cout << "\n=== Cycle-by-cycle execution ===" << std::endl;
    const char* read_type_names[] = {
        "???", "Data", "Instr", "Addr", "Unint", "Opcode", "Interrupt"
    };
    for (int cycle = 0; cycle < 12; ++cycle) {
        uint16_t abus = machine.cpu().abus.w;
        uint8_t dbus = machine.cpu().dbus;
        uint8_t read = machine.cpu().read;
        bool about_to_exec = M6502_IsAboutToExecute(&machine.cpu());

        const char* rt = (read < 7) ? read_type_names[read] : "???";
        std::cout << "Cycle " << cycle << ": "
                  << "PC=$" << std::hex << std::setw(4) << machine.cpu().pc.w
                  << " abus=$" << std::setw(4) << abus
                  << " dbus=$" << std::setw(2) << static_cast<int>(dbus)
                  << " read=" << static_cast<int>(read) << "(" << rt << ")"
                  << (about_to_exec ? " [EXEC]" : "")
                  << std::endl;

        machine.step();

        if (M6502_IsAboutToExecute(&machine.cpu())) {
            std::cout << "  -> Next instruction starts at PC=$" << std::hex << machine.cpu().pc.w << std::endl;
            break;
        }
    }

    // Check if $0400 was written
    std::cout << "\n=== After STA ($00),Y ===" << std::endl;
    std::cout << "[$0400]=$" << std::hex << static_cast<int>(machine.read(0x0400)) << std::endl;

    REQUIRE(true);
}

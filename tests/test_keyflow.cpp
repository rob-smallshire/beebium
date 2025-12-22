#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>

using namespace beebium;

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

TEST_CASE("Trace keyboard flow", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Run until $E9DD (JSR turnOnKeyboardLightsAndTestEscape)
    while (machine.cpu().opcode_pc.w != 0xE9DD) machine.step_instruction();
    
    std::cout << "At JSR $EA8D (turnOnKeyboardLightsAndTestEscape)" << std::endl;
    std::cout << "A=$" << std::hex << (int)machine.cpu().a << std::endl;
    
    // Trace through until we return from this JSR
    std::cout << "\n=== Tracing every instruction ===" << std::endl;
    int depth = 0;
    for (int i = 0; i < 200; ++i) {
        uint16_t pc = machine.cpu().opcode_pc.w;
        uint8_t a = machine.cpu().a;
        uint8_t x = machine.cpu().x;
        uint8_t opcode = machine.read(pc);
        
        // Track JSR/RTS depth
        if (opcode == 0x20) depth++;
        if (opcode == 0x60) depth--;
        
        std::cout << std::hex << std::setw(4) << std::setfill('0') << pc 
                  << "  A=$" << std::setw(2) << (int)a
                  << " X=$" << std::setw(2) << (int)x;
        
        // If reading VIA Port A, show what we'd read
        if (pc == 0xF037 || pc == 0xF03A) {  // LDX $FE4F area
            uint8_t via_porta = machine.read(0xFE4F);
            std::cout << " [VIA PA=$" << std::setw(2) << (int)via_porta << "]";
        }
        
        std::cout << std::endl;
        
        machine.step_instruction();
        
        // If depth < 0, we've returned from the initial JSR
        if (depth < 0) {
            std::cout << "\nReturned from JSR with A=$" << std::hex << (int)machine.cpu().a << std::endl;
            break;
        }
    }
    
    REQUIRE(true);
}

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

TEST_CASE("Trace ROL in OSBYTE 118", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Run until $CAE0
    while (machine.cpu().opcode_pc.w != 0xCAE0) machine.step_instruction();
    
    std::cout << "=== First CAE0 iteration ===" << std::endl;
    
    // Track a few iterations of the loop
    for (int iter = 0; iter < 3; ++iter) {
        std::cout << "\n--- Iteration " << iter << " ---" << std::endl;
        std::cout << "Entry: PC=$" << std::hex << machine.cpu().opcode_pc.w
                  << " A=$" << (int)machine.cpu().a
                  << " P=$" << (int)machine.cpu().p.value << std::endl;
        
        // Step through and watch for $E9E7 (PLP) and $E9E8 (ROL)
        bool found_rol = false;
        for (int i = 0; i < 2000 && !found_rol; ++i) {
            uint16_t pc = machine.cpu().opcode_pc.w;
            
            if (pc == 0xE9E7) {  // PLP
                std::cout << "At PLP ($E9E7): A=$" << std::hex << (int)machine.cpu().a
                          << " P=$" << (int)machine.cpu().p.value << std::endl;
            }
            if (pc == 0xE9E8) {  // ROL
                std::cout << "At ROL ($E9E8): A=$" << std::hex << (int)machine.cpu().a
                          << " P=$" << (int)machine.cpu().p.value
                          << " (C=" << (machine.cpu().p.value & 1) << ")" << std::endl;
                machine.step_instruction();  // Execute ROL
                std::cout << "After ROL:     A=$" << std::hex << (int)machine.cpu().a
                          << " P=$" << (int)machine.cpu().p.value
                          << " (N=" << ((machine.cpu().p.value >> 7) & 1)
                          << " C=" << (machine.cpu().p.value & 1) << ")" << std::endl;
                found_rol = true;
            }
            
            if (pc == 0xCAE6 || pc == 0xCAE8) {  // BCC or BMI
                std::cout << "At $" << std::hex << pc 
                          << ": P=$" << (int)machine.cpu().p.value
                          << " (N=" << ((machine.cpu().p.value >> 7) & 1)
                          << " C=" << (machine.cpu().p.value & 1) << ")" << std::endl;
            }
            
            if (!found_rol) machine.step_instruction();
            
            if (pc == 0xCAE0 && i > 0) {
                std::cout << "Back at CAE0 after " << i << " instructions" << std::endl;
                break;
            }
        }
    }
    
    REQUIRE(true);
}

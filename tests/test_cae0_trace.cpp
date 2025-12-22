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

TEST_CASE("Trace $CAE0 loop", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Run until we reach $CAE0
    int instr = 0;
    while (machine.cpu().opcode_pc.w != 0xCAE0 && instr < 200000) {
        machine.step_instruction();
        instr++;
    }
    
    std::cout << "Reached $CAE0 at instruction " << instr << std::endl;
    
    // Trace the next 50 instructions
    std::cout << "\n=== Tracing 50 instructions from $CAE0 ===" << std::endl;
    for (int i = 0; i < 50; ++i) {
        uint16_t pc = machine.cpu().opcode_pc.w;
        uint8_t opcode = machine.read(pc);
        uint8_t a = machine.cpu().a;
        uint8_t p = machine.cpu().p.value;
        
        std::cout << std::hex << std::setw(4) << std::setfill('0') << pc 
                  << "  " << std::setw(2) << (int)opcode
                  << "  A=$" << std::setw(2) << (int)a
                  << " P=" << std::setw(2) << (int)p
                  << " (N=" << (int)((p>>7)&1) 
                  << " C=" << (int)(p&1) << ")"
                  << std::endl;
        
        machine.step_instruction();
    }
    
    // Run a few more CAE0 iterations and check what's happening
    std::cout << "\n=== Checking CAE0 loop state ===" << std::endl;
    int cae0_count = 0;
    for (int i = 0; i < 10000 && cae0_count < 10; ++i) {
        if (machine.cpu().opcode_pc.w == 0xCAE0) {
            std::cout << "CAE0 iter " << cae0_count 
                      << ": A=$" << std::hex << (int)machine.cpu().a
                      << " P=$" << (int)machine.cpu().p.value
                      << " after $E9D9 return" << std::endl;
            cae0_count++;
        }
        machine.step_instruction();
    }
    
    REQUIRE(true);
}

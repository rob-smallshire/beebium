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

TEST_CASE("Trace A register source", "[boot][debug]") {
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
    
    std::cout << "=== Tracing one OSBYTE 118 call ===" << std::endl;
    std::cout << "Entry to $CAE0: A=$" << std::hex << (int)machine.cpu().a 
              << " P=$" << (int)machine.cpu().p.value << std::endl;
    
    // Step through JSR $CB14
    machine.step_instruction();  // JSR $CB14
    
    // Now trace through until $E9E8 (ROL), watching when A changes
    uint8_t last_a = machine.cpu().a;
    std::cout << "\nTracing A register changes:\n";
    
    for (int i = 0; i < 500; ++i) {
        uint16_t pc = machine.cpu().opcode_pc.w;
        uint8_t a = machine.cpu().a;
        uint8_t opcode = machine.read(pc);
        
        // Print if A changed
        if (a != last_a) {
            const char* op_name = "???";
            if (opcode == 0xA9) op_name = "LDA#";
            else if (opcode == 0xA5 || opcode == 0xA1 || opcode == 0xB1 || opcode == 0xB5) op_name = "LDA";
            else if (opcode == 0xAD || opcode == 0xBD || opcode == 0xB9) op_name = "LDA";
            else if (opcode == 0x68) op_name = "PLA";
            else if (opcode == 0x0A || opcode == 0x2A) op_name = "ASL/ROL";
            else if (opcode == 0x4A || opcode == 0x6A) op_name = "LSR/ROR";
            else if (opcode == 0x29) op_name = "AND#";
            else if (opcode == 0x09) op_name = "ORA#";
            else if (opcode == 0x49) op_name = "EOR#";
            else if (opcode == 0x69 || opcode == 0x65 || opcode == 0x6D) op_name = "ADC";
            else if (opcode == 0xE9 || opcode == 0xE5 || opcode == 0xED) op_name = "SBC";
            else if (opcode == 0x8A) op_name = "TXA";
            else if (opcode == 0x98) op_name = "TYA";
            
            std::cout << "  PC=$" << std::hex << std::setw(4) << std::setfill('0') << pc
                      << " " << op_name << " A: $" << std::setw(2) << (int)last_a
                      << " -> $" << std::setw(2) << (int)a << std::endl;
            last_a = a;
        }
        
        if (pc == 0xE9E8) {  // ROL - stop here
            std::cout << "\nReached ROL at $E9E8 with A=$" << std::hex << (int)a << std::endl;
            break;
        }
        
        machine.step_instruction();
    }
    
    REQUIRE(true);
}

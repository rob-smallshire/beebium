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

TEST_CASE("Trace stack for $F4 value", "[boot][debug]") {
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
    
    std::cout << "At $CAE0, SP=$" << std::hex << (int)machine.cpu().s.b.l << std::endl;
    
    // Trace PHA/PLA operations until $E9E8 (ROL)
    std::cout << "\nTracing stack operations to ROL at $E9E8:" << std::endl;
    for (int i = 0; i < 500; ++i) {
        uint16_t pc = machine.cpu().opcode_pc.w;
        uint8_t opcode = machine.read(pc);
        uint8_t sp = machine.cpu().s.b.l;
        uint8_t a = machine.cpu().a;
        
        // PHA
        if (opcode == 0x48) {
            std::cout << "PHA at $" << std::hex << std::setw(4) << pc
                      << ": A=$" << std::setw(2) << (int)a
                      << " -> stack[$1" << std::setw(2) << (int)sp << "]"
                      << std::endl;
        }
        // PHP
        if (opcode == 0x08) {
            uint8_t p = machine.cpu().p.value;
            std::cout << "PHP at $" << std::hex << std::setw(4) << pc
                      << ": P=$" << std::setw(2) << (int)p
                      << " -> stack[$1" << std::setw(2) << (int)sp << "]"
                      << std::endl;
        }
        // PLA
        if (opcode == 0x68) {
            uint8_t stack_val = machine.read(0x100 + sp + 1);
            std::cout << "PLA at $" << std::hex << std::setw(4) << pc
                      << ": stack[$1" << std::setw(2) << (int)(sp+1) << "]=$"
                      << std::setw(2) << (int)stack_val << " -> A"
                      << std::endl;
        }
        // PLP
        if (opcode == 0x28) {
            uint8_t stack_val = machine.read(0x100 + sp + 1);
            std::cout << "PLP at $" << std::hex << std::setw(4) << pc
                      << ": stack[$1" << std::setw(2) << (int)(sp+1) << "]=$"
                      << std::setw(2) << (int)stack_val << " -> P"
                      << std::endl;
        }
        // JSR
        if (opcode == 0x20) {
            uint16_t target = machine.read(pc+1) | (machine.read(pc+2) << 8);
            uint16_t ret = pc + 2;
            std::cout << "JSR $" << std::hex << std::setw(4) << target
                      << " at $" << std::setw(4) << pc
                      << ": push ret=$" << std::setw(4) << ret
                      << " (hi=" << std::setw(2) << (int)(ret >> 8)
                      << " lo=" << std::setw(2) << (int)(ret & 0xFF) << ")"
                      << std::endl;
        }
        
        if (pc == 0xE9E8) {
            std::cout << "\nAt ROL ($E9E8): A=$" << std::hex << (int)machine.cpu().a << std::endl;
            break;
        }
        
        machine.step_instruction();
    }
    
    REQUIRE(true);
}

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

TEST_CASE("Trace how we reach $CAE0", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Track JSRs that might lead to $CAE0
    std::vector<uint16_t> call_stack;
    
    std::cout << "Tracing call stack to $CAE0:" << std::endl;
    
    for (int i = 0; i < 300000; ++i) {
        uint16_t pc = machine.cpu().opcode_pc.w;
        uint8_t opcode = machine.read(pc);
        
        // Track JSR
        if (opcode == 0x20) {
            uint16_t target = machine.read(pc+1) | (machine.read(pc+2) << 8);
            call_stack.push_back(target);
            if (call_stack.size() > 20) call_stack.erase(call_stack.begin());
        }
        // Track RTS
        if (opcode == 0x60 && !call_stack.empty()) {
            call_stack.pop_back();
        }
        // Track JMP
        if (opcode == 0x4C) {
            uint16_t target = machine.read(pc+1) | (machine.read(pc+2) << 8);
            // Clear call stack for absolute jumps outside subroutine context
        }
        
        if (pc == 0xCAE0) {
            std::cout << "Reached $CAE0 at instruction " << i << std::endl;
            std::cout << "Recent call stack:" << std::endl;
            for (size_t j = 0; j < call_stack.size(); ++j) {
                std::cout << "  " << j << ": $" << std::hex << call_stack[j] << std::endl;
            }
            
            // Also show stack contents for return addresses
            uint8_t sp = machine.cpu().s.b.l;
            std::cout << "Stack (SP=$" << std::hex << (int)sp << "):" << std::endl;
            for (int j = 0; j < 8; j += 2) {
                uint8_t lo = machine.read(0x100 + sp + 1 + j);
                uint8_t hi = machine.read(0x100 + sp + 2 + j);
                uint16_t addr = lo | (hi << 8);
                std::cout << "  $" << std::hex << (addr + 1) << " (caller)" << std::endl;
            }
            break;
        }
        
        machine.step_instruction();
    }
    
    REQUIRE(true);
}

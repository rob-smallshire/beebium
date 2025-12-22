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

TEST_CASE("Trace startUpOptions calculation", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Run until startUpOptions is written (it's at $DA3F: STA $028F)
    // Look for writes to $028F
    machine.add_watchpoint(0x028F, 1, WATCH_WRITE,
        [&](uint16_t addr, uint8_t val, bool is_write, uint64_t cycle) {
            std::cout << "Write $028F = $" << std::hex << (int)val 
                      << " at PC=$" << machine.cpu().opcode_pc.w
                      << " cycle=" << std::dec << cycle << std::endl;
        });
    
    // Run until we pass the mode detection phase
    for (int i = 0; i < 200000; ++i) {
        if (machine.read(0x028F) != 0) break;
        machine.step_instruction();
    }
    
    uint8_t startup_options = machine.read(0x028F);
    std::cout << "startUpOptions ($028F) = $" << std::hex << (int)startup_options << std::endl;
    std::cout << "Mode = " << std::dec << (int)(startup_options & 0x07) << std::endl;
    
    REQUIRE(true);
}

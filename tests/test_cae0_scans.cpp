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

TEST_CASE("Trace scans during $CAE0", "[boot][debug]") {
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
    
    std::cout << "At $CAE0, tracing Port A reads:" << std::endl;
    
    // Track Port A reads during one CAE0 iteration
    std::vector<std::pair<uint8_t, uint8_t>> reads;  // key_number, result
    machine.add_watchpoint(0xFE4F, 1, WATCH_READ,
        [&](uint16_t, uint8_t val, bool, uint64_t) {
            uint8_t key = machine.memory().system_via_peripheral.last_scanned_key();
            reads.push_back({key, val});
        });
    
    // Execute until we return to $CAE0
    int iter = 0;
    for (iter = 0; iter < 500; ++iter) {
        machine.step_instruction();
        if (machine.cpu().opcode_pc.w == 0xCAE0 && iter > 0) break;
    }
    machine.clear_watchpoints();
    
    std::cout << "Port A reads during one CAE0 iteration (" << reads.size() << " total):" << std::endl;
    for (const auto& [key, val] : reads) {
        uint8_t col = key & 0x0F;
        uint8_t row = (key >> 4) & 0x07;
        std::cout << "  Key $" << std::hex << std::setw(2) << std::setfill('0') << (int)key
                  << " (col=" << (int)col << " row=" << (int)row << ")"
                  << " -> $" << std::setw(2) << (int)val
                  << " (bit7=" << ((val >> 7) & 1) << ")"
                  << std::endl;
    }
    
    REQUIRE(true);
}

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/ProgramCounterHistogram.hpp>
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

TEST_CASE("Debug clearRAM loop", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.reset();

    // Skip reset sequence
    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Execute until we reach clearRAM at $D9E7
    int instructions = 0;
    while (machine.cpu().pc.w != 0xD9E7 && instructions < 100) {
        machine.step_instruction();
        instructions++;
    }

    std::cout << "Reached clearRAM entry at cycle " << machine.cycle_count() << std::endl;
    std::cout << "PC: $" << std::hex << machine.cpu().pc.w << std::endl;

    // Trace first 50 instructions of clearRAM
    std::cout << "\n=== First 50 instructions of clearRAM ===" << std::endl;
    for (int i = 0; i < 50; ++i) {
        uint16_t pc = machine.cpu().pc.w;
        uint8_t op = machine.read(pc);
        uint8_t a = machine.cpu().a;
        uint8_t x = machine.cpu().x;
        uint8_t y = machine.cpu().y;
        uint8_t zp00 = machine.read(0x00);
        uint8_t zp01 = machine.read(0x01);
        uint8_t p = machine.cpu().p.value;
        
        // Check IFR/IER
        uint8_t ier = machine.memory().system_via.state().ier.value;
        uint8_t ifr = machine.memory().system_via.state().ifr.value;
        
        std::cout << std::hex << std::setw(4) << std::setfill('0') << pc << " "
                  << std::setw(2) << (int)op << " "
                  << "A=" << std::setw(2) << (int)a << " "
                  << "X=" << std::setw(2) << (int)x << " "
                  << "Y=" << std::setw(2) << (int)y << " "
                  << "P=" << std::setw(2) << (int)p << " "
                  << "$00=" << std::setw(2) << (int)zp00 << " "
                  << "$01=" << std::setw(2) << (int)zp01 << " "
                  << "IER=" << std::setw(2) << (int)ier << " "
                  << "IFR=" << std::setw(2) << (int)ifr << std::endl;
        
        machine.step_instruction();
    }

    // Now run 10000 more instructions and check progress
    std::cout << "\n=== Running 10000 more instructions ===" << std::endl;
    ProgramCounterHistogram pc_histogram;
    pc_histogram.attach(machine);
    for (int i = 0; i < 10000; ++i) {
        machine.step_instruction();
    }
    pc_histogram.detach(machine);

    std::cout << "After 10000 instructions:" << std::endl;
    std::cout << "$01 (page counter): $" << std::hex << (int)machine.read(0x01) << std::endl;
    std::cout << "Expected to reach page $80 to exit loop" << std::endl;

    // Show most visited PCs using histogram's top_addresses()
    auto top_pcs = pc_histogram.top_addresses(10);

    std::cout << "\nTop 10 most visited PCs:" << std::endl;
    for (const auto& [pc, count] : top_pcs) {
        std::cout << "  $" << std::hex << std::setw(4) << std::setfill('0') << pc
                  << ": " << std::dec << count << " visits" << std::endl;
    }

    REQUIRE(true);
}

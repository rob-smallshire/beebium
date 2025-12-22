#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

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

TEST_CASE("Count keyboard scans before $CAE0", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Count VIA Port A reads until we reach $CAE0
    int porta_reads = 0;
    machine.add_watchpoint(0xFE4F, 1, WATCH_READ,
        [&](uint16_t, uint8_t, bool, uint64_t) {
            porta_reads++;
        });
    
    while (machine.cpu().opcode_pc.w != 0xCAE0) {
        machine.step_instruction();
    }
    machine.clear_watchpoints();
    
    std::cout << "Port A reads before $CAE0: " << porta_reads << std::endl;
    REQUIRE(true);
}

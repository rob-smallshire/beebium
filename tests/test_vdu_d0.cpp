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

TEST_CASE("Check $D0 at $CAE0", "[boot][debug]") {
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
    
    // Check $D0 (vduStatus)
    uint8_t d0 = machine.read(0x00D0);
    std::cout << "At $CAE0:" << std::endl;
    std::cout << "  $D0 (vduStatus) = $" << std::hex << (int)d0 << std::endl;
    
    // Check what ($D0 EOR $04) AND $46 would give
    uint8_t check = (d0 ^ 0x04) & 0x46;
    std::cout << "  ($D0 EOR $04) AND $46 = $" << std::hex << (int)check << std::endl;
    std::cout << "  BNE would " << (check != 0 ? "exit (skip loop)" : "NOT exit (continue to loop check)") << std::endl;
    
    // Check other relevant VDU variables
    std::cout << "\nOther VDU state:" << std::endl;
    std::cout << "  $0268 (pagedMode) = $" << std::hex << (int)machine.read(0x0268) << std::endl;
    std::cout << "  $0269 (scrollFlag) = $" << std::hex << (int)machine.read(0x0269) << std::endl;
    std::cout << "  $026A (lineCount) = $" << std::hex << (int)machine.read(0x026A) << std::endl;
    
    REQUIRE(true);
}

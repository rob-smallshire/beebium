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

TEST_CASE("Debug paged mode during boot", "[boot][debug]") {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;
    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.reset();

    // Skip reset sequence
    while (!M6502_IsAboutToExecute(&machine.cpu())) machine.step();

    // Track when paged mode flag changes
    std::cout << "=== Tracking paged mode during boot ===" << std::endl;
    
    uint8_t last_paged_mode = machine.read(0x0268);
    uint8_t last_page_count = machine.read(0x026A); // paged mode line count
    
    // Run boot and check for $CAE0 visits
    int cae0_visits = 0;
    int first_cae0_instr = -1;
    
    for (int i = 0; i < 500000; ++i) {
        uint16_t pc = machine.cpu().opcode_pc.w;
        
        // Check paged mode changes
        uint8_t paged_mode = machine.read(0x0268);
        uint8_t page_count = machine.read(0x026A);
        if (paged_mode != last_paged_mode || page_count != last_page_count) {
            std::cout << "Instr " << i << ": Paged mode=$" << std::hex << (int)paged_mode
                      << " count=$" << (int)page_count 
                      << " PC=$" << pc << std::endl;
            last_paged_mode = paged_mode;
            last_page_count = page_count;
        }
        
        // Track $CAE0 visits
        if (pc == 0xCAE0) {
            if (first_cae0_instr < 0) {
                first_cae0_instr = i;
                std::cout << "\n*** First visit to $CAE0 at instruction " << i << " ***" << std::endl;
                std::cout << "  A=$" << std::hex << (int)machine.cpu().a
                          << " X=$" << (int)machine.cpu().x
                          << " Y=$" << (int)machine.cpu().y << std::endl;
                std::cout << "  Paged mode ($0268)=$" << (int)machine.read(0x0268) << std::endl;
                std::cout << "  Page count ($026A)=$" << (int)machine.read(0x026A) << std::endl;
                std::cout << "  Window height ($026B)=$" << (int)machine.read(0x026B) << std::endl;
                std::cout << "  Scroll flag ($0269)=$" << (int)machine.read(0x0269) << std::endl;
            }
            cae0_visits++;
            if (cae0_visits > 10000) {
                std::cout << "\nExiting after " << cae0_visits << " visits to $CAE0" << std::endl;
                break;
            }
        }
        
        machine.step_instruction();
    }
    
    std::cout << "\n=== Final state ===" << std::endl;
    std::cout << "Total $CAE0 visits: " << cae0_visits << std::endl;
    std::cout << "Paged mode ($0268): $" << std::hex << (int)machine.read(0x0268) << std::endl;
    std::cout << "Page count ($026A): $" << std::hex << (int)machine.read(0x026A) << std::endl;
    
    REQUIRE(true);
}

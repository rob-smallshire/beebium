#include <catch2/catch_test_macros.hpp>
#include <6502/6502.h>
#include <array>
#include <fstream>
#include <vector>
#include <cstring>

// Test harness for 6502 CPU tests

namespace {

// Simple 64KB memory for testing
std::array<uint8_t, 65536> g_mem;

// Run a 6502 test binary
// Returns true if the test passes (writes 0 to 0xFF00)
// Returns false if the test fails (writes non-zero to 0xFF00 or enters infinite loop)
bool run_klaus_test(const std::vector<uint8_t>& data,
                    uint16_t load_address,
                    uint16_t init_pc,
                    const M6502Config* config,
                    size_t max_cycles = 100'000'000) {
    // Initialize memory
    std::fill(g_mem.begin(), g_mem.end(), 0);

    // Load test binary
    if (load_address + data.size() > g_mem.size()) {
        return false;
    }
    std::memcpy(g_mem.data() + load_address, data.data(), data.size());

    // Initialize CPU
    M6502 cpu;
    M6502_Init(&cpu, config);
    cpu.tfn = &M6502_NextInstruction;
    cpu.pc.w = init_pc;

    int last_pc = -1;
    size_t cycles = 0;

    while (cycles < max_cycles) {
        if (M6502_IsAboutToExecute(&cpu)) {
            // Check for infinite loop (same PC twice in a row)
            if (cpu.abus.w == last_pc) {
                M6502_Destroy(&cpu);
                return false;  // Stuck in infinite loop
            }
            last_pc = cpu.abus.w;
        }

        // Execute one cycle
        (*cpu.tfn)(&cpu);
        cycles++;

        // Handle memory access
        if (cpu.read) {
            cpu.dbus = g_mem[cpu.abus.w];
        } else {
            // Check for test completion signal
            if (cpu.abus.w == 0xFF00) {
                bool success = (cpu.dbus == 0);
                M6502_Destroy(&cpu);
                return success;
            }
            g_mem[cpu.abus.w] = cpu.dbus;
        }
    }

    M6502_Destroy(&cpu);
    return false;  // Timeout
}

// Load a binary file from the assets directory
std::vector<uint8_t> load_test_binary(const std::string& filename) {
    // Test binaries are in tests/assets/klaus/
    std::string filepath = std::string(BEEBIUM_TEST_ASSETS_DIR) + "/klaus/" + filename;

    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return {};
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

} // namespace

TEST_CASE("6502 Klaus functional test - NMOS", "[6502][klaus]") {
    auto data = load_test_binary("6502.bin");
    REQUIRE(!data.empty());

    bool result = run_klaus_test(data, 0, 0x400, &M6502_nmos6502_config);
    REQUIRE(result);
}

TEST_CASE("6502 basic operations", "[6502][basic]") {
    std::fill(g_mem.begin(), g_mem.end(), 0);

    // Simple test: LDA #$42, STA $0200, BRK
    // $0400: A9 42     LDA #$42
    // $0402: 8D 00 02  STA $0200
    // $0405: 00        BRK
    g_mem[0x0400] = 0xA9;  // LDA immediate
    g_mem[0x0401] = 0x42;  // #$42
    g_mem[0x0402] = 0x8D;  // STA absolute
    g_mem[0x0403] = 0x00;  // low byte
    g_mem[0x0404] = 0x02;  // high byte
    g_mem[0x0405] = 0x00;  // BRK

    // Reset vector points to $0400
    g_mem[0xFFFC] = 0x00;
    g_mem[0xFFFD] = 0x04;

    M6502 cpu;
    M6502_Init(&cpu, &M6502_nmos6502_config);
    M6502_Reset(&cpu);

    // Run until BRK is executed
    size_t cycles = 0;
    while (cycles < 1000) {
        (*cpu.tfn)(&cpu);
        cycles++;

        if (cpu.read) {
            cpu.dbus = g_mem[cpu.abus.w];
        } else {
            g_mem[cpu.abus.w] = cpu.dbus;
        }

        // Check if we've executed the STA and are past it
        if (M6502_IsAboutToExecute(&cpu) && cpu.abus.w >= 0x0405) {
            break;
        }
    }

    REQUIRE(cpu.a == 0x42);
    REQUIRE(g_mem[0x0200] == 0x42);

    M6502_Destroy(&cpu);
}

TEST_CASE("6502 CPU initialization", "[6502][init]") {
    M6502 cpu;
    M6502_Init(&cpu, &M6502_nmos6502_config);

    REQUIRE(cpu.config == &M6502_nmos6502_config);
    REQUIRE(cpu.tfn != nullptr);

    M6502_Destroy(&cpu);
}

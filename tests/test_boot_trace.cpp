// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>
#include <beebium/ProgramCounterHistogram.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

using namespace beebium;

namespace {

std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool roms_available() {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "OS12.ROM") &&
           std::filesystem::exists(rom_dir / "BASIC2.ROM");
}

// Helper to disassemble a single opcode (simplified)
const char* opcode_name(uint8_t op) {
    static const char* names[256] = {
        "BRK","ORA","???","???","???","ORA","ASL","???","PHP","ORA","ASL","???","???","ORA","ASL","???",
        "BPL","ORA","???","???","???","ORA","ASL","???","CLC","ORA","???","???","???","ORA","ASL","???",
        "JSR","AND","???","???","BIT","AND","ROL","???","PLP","AND","ROL","???","BIT","AND","ROL","???",
        "BMI","AND","???","???","???","AND","ROL","???","SEC","AND","???","???","???","AND","ROL","???",
        "RTI","EOR","???","???","???","EOR","LSR","???","PHA","EOR","LSR","???","JMP","EOR","LSR","???",
        "BVC","EOR","???","???","???","EOR","LSR","???","CLI","EOR","???","???","???","EOR","LSR","???",
        "RTS","ADC","???","???","???","ADC","ROR","???","PLA","ADC","ROR","???","JMP","ADC","ROR","???",
        "BVS","ADC","???","???","???","ADC","ROR","???","SEI","ADC","???","???","???","ADC","ROR","???",
        "???","STA","???","???","STY","STA","STX","???","DEY","???","TXA","???","STY","STA","STX","???",
        "BCC","STA","???","???","STY","STA","STX","???","TYA","STA","TXS","???","???","STA","???","???",
        "LDY","LDA","LDX","???","LDY","LDA","LDX","???","TAY","LDA","TAX","???","LDY","LDA","LDX","???",
        "BCS","LDA","???","???","LDY","LDA","LDX","???","CLV","LDA","TSX","???","LDY","LDA","LDX","???",
        "CPY","CMP","???","???","CPY","CMP","DEC","???","INY","CMP","DEX","???","CPY","CMP","DEC","???",
        "BNE","CMP","???","???","???","CMP","DEC","???","CLD","CMP","???","???","???","CMP","DEC","???",
        "CPX","SBC","???","???","CPX","SBC","INC","???","INX","SBC","NOP","???","CPX","SBC","INC","???",
        "BEQ","SBC","???","???","???","SBC","INC","???","SED","SBC","???","???","???","SBC","INC","???"
    };
    return names[op];
}

} // namespace

TEST_CASE("MOS boot trace - detect infinite loop", "[boot][trace]") {
    if (!roms_available()) {
        SKIP("ROM files not available at " BEEBIUM_ROM_DIR);
    }

    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    machine.reset();

    // Track recent PC values to detect loops
    std::vector<uint16_t> pc_history;
    pc_history.reserve(10000);

    // Key boot milestones we expect to pass
    struct Milestone {
        uint16_t addr;
        const char* name;
        bool reached = false;
        uint64_t cycle = 0;
    };

    std::vector<Milestone> milestones = {
        {0xD9CE, "Reset entry (operand)"},  // PC after reset (opcode already fetched)
        {0xDA03, "Addressable latch init"}, // LDX #$0F before latch write
        {0xDCDB, "Copy vectors"},           // LDY #$12 for vector copy
        {0xDD2C, "Screen mode init"},       // After VDU init
        {0xE310, "Language entry"},         // Language ROM entry
    };

    // Boot addresses that indicate we're stuck
    std::set<uint16_t> loop_addresses = {
        0xCAE0, // Main keyboard polling loop
        0xE9D9, // Escape/keyboard check
        0xEF02, // KEYV entry
    };

    // Run up to 1M instructions
    uint64_t max_instructions = 1'000'000;
    uint64_t instruction_count = 0;
    bool loop_detected = false;
    uint16_t loop_start_pc = 0;
    uint64_t loop_start_cycle = 0;

    // For loop detection: use ProgramCounterHistogram
    ProgramCounterHistogram pc_histogram;
    pc_histogram.attach(machine);
    uint64_t loop_threshold = 50000;  // High threshold to get past legitimate boot loops (memory clearing, etc.)

    while (instruction_count < max_instructions) {
        uint16_t pc = machine.cpu().pc.w;

        // Check milestones
        for (auto& m : milestones) {
            if (!m.reached && pc == m.addr) {
                m.reached = true;
                m.cycle = machine.cycle_count();
                INFO("Milestone reached: " << m.name << " at cycle " << m.cycle);
            }
        }

        // Check for loop detection using histogram
        if (pc_histogram.exceeds_threshold(pc, loop_threshold)) {
            // We're probably in a loop
            loop_detected = true;
            loop_start_pc = pc;
            loop_start_cycle = machine.cycle_count();

            // Record context around the loop
            INFO("Loop detected at PC=0x" << std::hex << pc << " after "
                 << std::dec << pc_histogram.visits(pc) << " visits");
            INFO("Cycle: " << machine.cycle_count());
            INFO("A=0x" << std::hex << static_cast<int>(machine.cpu().a)
                 << " X=0x" << static_cast<int>(machine.cpu().x)
                 << " Y=0x" << static_cast<int>(machine.cpu().y));
            INFO("P=0x" << std::hex << static_cast<int>(machine.cpu().p.value)
                 << " SP=0x" << static_cast<int>(machine.cpu().s.b.l));
            INFO("Opcode: " << opcode_name(machine.read(pc))
                 << " (0x" << std::hex << static_cast<int>(machine.read(pc)) << ")");
            break;
        }

        // Check for screen memory write (sign of boot progress)
        // We can't easily detect writes without callbacks, but we can check periodically
        if (instruction_count % 10000 == 0) {
            // Check if "BBC" has appeared in screen memory
            for (uint16_t addr = 0x3000; addr <= 0x7FFD; ++addr) {
                if (machine.read(addr) == 'B' &&
                    machine.read(addr + 1) == 'B' &&
                    machine.read(addr + 2) == 'C') {
                    INFO("Found 'BBC' at 0x" << std::hex << addr << " after "
                         << std::dec << instruction_count << " instructions");
                    break;
                }
            }
        }

        // Execute one instruction
        machine.step_instruction();
        instruction_count++;
    }

    pc_histogram.detach(machine);

    // Report milestones
    std::cout << "\n=== Boot Milestones ===" << std::endl;
    for (const auto& m : milestones) {
        if (m.reached) {
            std::cout << "  [PASS] " << m.name << " at cycle " << std::dec << m.cycle << std::endl;
        } else {
            std::cout << "  [FAIL] " << m.name << " - not reached" << std::endl;
        }
    }

    // Show most visited PCs using histogram's top_addresses()
    auto top_pcs = pc_histogram.top_addresses(10);

    std::cout << "\n=== Top 10 Most Visited PCs ===" << std::endl;
    for (const auto& [addr, count] : top_pcs) {
        std::cout << "  0x" << std::hex << std::setw(4) << std::setfill('0') << addr
                  << ": " << std::dec << count
                  << " visits (" << opcode_name(machine.read(addr)) << ")" << std::endl;
    }

    // Show final state
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Instructions executed: " << std::dec << instruction_count << std::endl;
    std::cout << "Cycles: " << machine.cycle_count() << std::endl;
    std::cout << "Final PC: 0x" << std::hex << machine.cpu().pc.w << std::endl;
    std::cout << "A=0x" << std::hex << static_cast<int>(machine.cpu().a)
              << " X=0x" << static_cast<int>(machine.cpu().x)
              << " Y=0x" << static_cast<int>(machine.cpu().y)
              << " P=0x" << static_cast<int>(machine.cpu().p.value)
              << " SP=0x" << static_cast<int>(machine.cpu().s.b.l) << std::endl;

    // Check zero page locations used by memory clear loop
    std::cout << "\n=== Zero Page (memory clear loop) ===" << std::endl;
    std::cout << "ZP $00: 0x" << std::hex << static_cast<int>(machine.read(0x00)) << std::endl;
    std::cout << "ZP $01: 0x" << std::hex << static_cast<int>(machine.read(0x01)) << std::endl;

    // Check key memory locations
    std::cout << "\n=== MOS Variables ===" << std::endl;
    std::cout << "KEYV: 0x" << std::hex << (machine.read(0x0228) | (machine.read(0x0229) << 8)) << std::endl;
    std::cout << "IRQ1V: 0x" << std::hex << (machine.read(0x0204) | (machine.read(0x0205) << 8)) << std::endl;
    std::cout << "Reset type ($028A): " << std::dec << static_cast<int>(machine.read(0x028A)) << std::endl;

    // VIA state
    std::cout << "\n=== System VIA ===" << std::endl;
    std::cout << "IER: 0x" << std::hex << static_cast<int>(machine.memory().system_via.state().ier.value) << std::endl;
    std::cout << "IFR: 0x" << std::hex << static_cast<int>(machine.memory().system_via.state().ifr.value) << std::endl;
    std::cout << "ACR: 0x" << std::hex << static_cast<int>(machine.memory().system_via.state().acr.value) << std::endl;
    std::cout << "PCR: 0x" << std::hex << static_cast<int>(machine.memory().system_via.state().pcr.value) << std::endl;
    std::cout << "T1: " << std::dec << machine.memory().system_via.state().t1 << std::endl;
    std::cout << "Port A OR: 0x" << std::hex << static_cast<int>(machine.memory().system_via.state().port_a.or_) << std::endl;
    std::cout << "Port B OR: 0x" << std::hex << static_cast<int>(machine.memory().system_via.state().port_b.or_) << std::endl;
    std::cout << "Screen mem at $7C00: ";
    for (int i = 0; i < 20; ++i) {
        uint8_t c = machine.read(0x7C00 + i);
        std::cout << ((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.');
    }
    std::cout << std::endl;

    if (loop_detected) {
        WARN("Boot stuck in loop at 0x" << std::hex << loop_start_pc);
    }

    // For now, just assert we got past the reset entry point
    bool passed_reset = false;
    for (const auto& m : milestones) {
        if (m.addr == 0xD9CE && m.reached) {
            passed_reset = true;
            break;
        }
    }
    REQUIRE(passed_reset);
}

TEST_CASE("Watchpoint trace of boot", "[boot][watchpoint]") {
    if (!roms_available()) {
        SKIP("ROM files not available at " BEEBIUM_ROM_DIR);
    }

    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    machine.reset();

    // Track writes to key addresses
    struct WriteRecord {
        uint16_t addr;
        uint8_t value;
        uint64_t cycle;
    };
    std::vector<WriteRecord> screen_writes;
    std::vector<WriteRecord> via_writes;
    std::vector<WriteRecord> keyv_writes;

    // Screen memory watchpoint (Mode 7: 0x7C00-0x7FFF, Graphics: 0x3000-0x7BFF)
    machine.add_watchpoint(0x3000, 0x8000 - 0x3000, WATCH_WRITE,
        [&](uint16_t addr, uint8_t value, bool, uint64_t cycle) {
            if (value != 0x00 && screen_writes.size() < 100) {
                screen_writes.push_back({addr, value, cycle});
            }
        });

    // System VIA writes watchpoint
    machine.add_watchpoint(0xFE40, 0x20, WATCH_WRITE,
        [&](uint16_t addr, uint8_t value, bool, uint64_t cycle) {
            if (via_writes.size() < 100) {
                via_writes.push_back({addr, value, cycle});
            }
        });

    // KEYV vector watchpoint
    machine.add_watchpoint(0x0228, 2, WATCH_WRITE,
        [&](uint16_t addr, uint8_t value, bool, uint64_t cycle) {
            keyv_writes.push_back({addr, value, cycle});
        });

    // Run for 2M instructions to see if banner gets written
    uint64_t max_instructions = 2'000'000;
    for (uint64_t i = 0; i < max_instructions; ++i) {
        machine.step_instruction();
    }

    // Report findings
    std::cout << "\n=== Screen Memory Writes (first " << screen_writes.size() << ") ===" << std::endl;
    for (const auto& w : screen_writes) {
        char c = (w.value >= 0x20 && w.value < 0x7F) ? static_cast<char>(w.value) : '.';
        std::cout << "  $" << std::hex << std::setw(4) << std::setfill('0') << w.addr
                  << " <- 0x" << std::setw(2) << static_cast<int>(w.value)
                  << " '" << c << "'"
                  << " @ cycle " << std::dec << w.cycle << std::endl;
    }

    std::cout << "\n=== KEYV Writes ===" << std::endl;
    for (const auto& w : keyv_writes) {
        std::cout << "  $" << std::hex << std::setw(4) << std::setfill('0') << w.addr
                  << " <- 0x" << std::setw(2) << static_cast<int>(w.value)
                  << " @ cycle " << std::dec << w.cycle << std::endl;
    }

    std::cout << "\n=== System VIA Writes (first " << std::min(via_writes.size(), size_t(20)) << ") ===" << std::endl;
    for (size_t i = 0; i < std::min(via_writes.size(), size_t(20)); ++i) {
        const auto& w = via_writes[i];
        std::cout << "  $" << std::hex << std::setw(4) << std::setfill('0') << w.addr
                  << " <- 0x" << std::setw(2) << static_cast<int>(w.value)
                  << " @ cycle " << std::dec << w.cycle << std::endl;
    }

    // Final state
    std::cout << "\n=== Final State ===" << std::endl;
    std::cout << "Final PC: 0x" << std::hex << machine.cpu().pc.w << std::endl;
    std::cout << "KEYV: 0x" << std::hex << (machine.read(0x0228) | (machine.read(0x0229) << 8)) << std::endl;
    std::cout << "Screen writes total: " << std::dec << screen_writes.size() << std::endl;

    // Check if we have screen writes
    INFO("Screen writes: " << screen_writes.size());
    REQUIRE(true);  // Informational test - always passes
}

TEST_CASE("Trace first 100 instructions of boot", "[boot][trace][.detailed]") {  // Hidden by default
    if (!roms_available()) {
        SKIP("ROM files not available at " BEEBIUM_ROM_DIR);
    }

    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    machine.reset();

    // Skip first few cycles to let reset sequence complete
    // The 6502 reset takes ~7 cycles before fetching from reset vector
    std::cout << "=== Running reset sequence ===" << std::endl;
    for (int i = 0; i < 10; ++i) {
        uint16_t pc = machine.cpu().pc.w;
        std::cout << "Cycle " << i << ": PC=0x" << std::hex << pc << std::endl;
        machine.step();
    }

    std::cout << "\n=== First 100 Instructions of Boot ===" << std::endl;

    for (int i = 0; i < 100; ++i) {
        uint16_t pc = machine.cpu().pc.w;
        uint8_t opcode = machine.read(pc);
        uint8_t a = machine.cpu().a;
        uint8_t x = machine.cpu().x;
        uint8_t y = machine.cpu().y;
        uint8_t p = machine.cpu().p.value;
        uint8_t sp = machine.cpu().s.b.l;

        // Format: PC  OPCODE  NAME  A  X  Y  P  SP
        std::cout << std::hex << std::setw(4) << std::setfill('0') << pc << "  "
                  << std::setw(2) << static_cast<int>(opcode) << "  "
                  << std::setw(3) << std::setfill(' ') << opcode_name(opcode) << "  "
                  << "A=" << std::setw(2) << std::setfill('0') << static_cast<int>(a) << " "
                  << "X=" << std::setw(2) << static_cast<int>(x) << " "
                  << "Y=" << std::setw(2) << static_cast<int>(y) << " "
                  << "P=" << std::setw(2) << static_cast<int>(p) << " "
                  << "SP=" << std::setw(2) << static_cast<int>(sp) << std::endl;

        machine.step_instruction();
    }

    REQUIRE(true);  // Just for output
}

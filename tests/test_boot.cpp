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
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

using namespace beebium;

namespace {

// Load a ROM file into a vector
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

// Check if ROM files exist
bool roms_available() {
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "OS12.ROM") &&
           std::filesystem::exists(rom_dir / "BASIC2.ROM");
}

// ===========================================================================
// Watchpoint-based assertion framework
// ===========================================================================

// Expected memory access
struct ExpectedAccess {
    uint16_t addr;
    std::optional<uint8_t> expected_value;  // nullopt = any value
    bool is_write;
    std::string description;
};

// Result of waiting for an expected access
struct AccessResult {
    bool found = false;
    uint64_t cycle = 0;
    uint8_t actual_value = 0;
    std::string error_msg;
};

// Expect an access at EXACTLY the specified cycle (0 means next cycle)
// Returns the result with cycle info
AccessResult expect_access_at_cycle(ModelB& machine, const ExpectedAccess& expected, uint64_t exact_cycle) {
    AccessResult result;
    bool found = false;
    uint64_t found_cycle = 0;
    uint8_t found_value = 0;

    machine.clear_watchpoints();
    machine.add_watchpoint(expected.addr, 1, expected.is_write ? WATCH_WRITE : WATCH_READ,
        [&](uint16_t addr, uint8_t value, bool is_write, uint64_t cycle) {
            (void)addr;
            if (is_write == expected.is_write && !found) {
                found = true;
                found_cycle = cycle;
                found_value = value;
            }
        });

    // Step exactly to the expected cycle
    while (machine.cycle_count() < exact_cycle && !found) {
        machine.step();
    }
    // One more step if we're at the cycle but haven't seen the access yet
    if (!found && machine.cycle_count() == exact_cycle) {
        machine.step();
    }

    machine.clear_watchpoints();

    if (!found) {
        std::ostringstream ss;
        ss << "Expected " << (expected.is_write ? "write to" : "read from")
           << " $" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << expected.addr
           << " at cycle " << std::dec << exact_cycle
           << ", but it didn't occur (current cycle: " << machine.cycle_count() << "). "
           << "Description: " << expected.description;
        result.error_msg = ss.str();
        return result;
    }

    if (found_cycle != exact_cycle) {
        std::ostringstream ss;
        ss << "Access to $" << std::hex << std::uppercase << expected.addr
           << " occurred at cycle " << std::dec << found_cycle
           << " but expected cycle " << exact_cycle
           << ". Description: " << expected.description;
        result.error_msg = ss.str();
        return result;
    }

    if (expected.expected_value && found_value != *expected.expected_value) {
        std::ostringstream ss;
        ss << "Access to $" << std::hex << std::uppercase << expected.addr
           << ": expected value $" << static_cast<int>(*expected.expected_value)
           << " but got $" << static_cast<int>(found_value)
           << ". Description: " << expected.description;
        result.error_msg = ss.str();
        return result;
    }

    result.found = true;
    result.cycle = found_cycle;
    result.actual_value = found_value;
    return result;
}

// Assert access at exact cycle
#define ASSERT_ACCESS_AT(machine, expected, exact_cycle) \
    do { \
        auto _result = expect_access_at_cycle(machine, expected, exact_cycle); \
        if (!_result.error_msg.empty()) { \
            FAIL(_result.error_msg); \
        } \
    } while(0)

} // namespace

// ===========================================================================
// Test: 6502 Reset Cycle Timing
// ===========================================================================
// The 6502 reset sequence is documented in the MOS 6502 Programming Manual.
// Reset takes 7 cycles:
//   Cycle 1: Internal operation (address bus undefined)
//   Cycle 2: Internal operation
//   Cycle 3: Fake push PCH to stack (stack pointer not decremented)
//   Cycle 4: Fake push PCL to stack
//   Cycle 5: Fake push status to stack
//   Cycle 6: Read low byte of reset vector from $FFFC
//   Cycle 7: Read high byte of reset vector from $FFFD
// After cycle 7, the CPU loads PC from the vector and fetches the first opcode.
//
// Reference: MOS 6502 Programming Manual, Appendix A
// Also verified against visual6502.org transistor-level simulation

TEST_CASE("6502 reset vector timing", "[boot][deterministic][reset]") {
    if (!roms_available()) {
        SKIP("ROM files not available at " BEEBIUM_ROM_DIR);
    }

    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    // Verify reset vector contents before reset
    REQUIRE(machine.read(0xFFFC) == 0xCD);  // Low byte
    REQUIRE(machine.read(0xFFFD) == 0xD9);  // High byte = $D9CD

    machine.reset();
    REQUIRE(machine.cycle_count() == 0);

    // Assert: reset vector low byte read at EXACTLY cycle 4 (0-indexed)
    // B2 6502 library: cycles 0-3 internal, cycle 4 read $FFFC, cycle 5 read $FFFD
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0xFFFC, 0xCD, false, "Reset vector low byte"}),
        4);


    // Assert: reset vector high byte read at EXACTLY cycle 5 (0-indexed)
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0xFFFD, 0xD9, false, "Reset vector high byte"}),
        5);

    // Complete reset (fetch first opcode)
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Assert: total reset sequence takes exactly 7 cycles
    REQUIRE(machine.cycle_count() == 7);

    // Assert: PC now points past the first opcode (operand address)
    REQUIRE(machine.cpu().pc.w == 0xD9CE);  // D9CD + 1 (opcode fetched)
}

// ===========================================================================
// Test: First Instruction Execution (LDA #$40)
// ===========================================================================
// OS12.ROM reset entry point at $D9CD:
//   D9CD: A9 40     LDA #$40
// LDA immediate takes 2 cycles:
//   Cycle 1: Fetch operand
//   Cycle 2: Execute (already have opcode from previous fetch)
// Actually in the 6502, cycle 1 already executed the decode, so next step
// reads the operand and loads A.

TEST_CASE("First instruction LDA #$40 execution", "[boot][deterministic][instruction]") {
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

    // Complete reset sequence
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    REQUIRE(machine.cycle_count() == 7);

    // LDA # timing:
    //   Cycle 7: Opcode already fetched (during reset), execute decode
    //   Cycle 8: Read operand from PC, load into A
    // After cycle 8, next opcode is fetched

    // Assert: operand read at EXACTLY cycle 7 (the first cycle of instruction execution)
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0xD9CE, 0x40, false, "LDA #$40 operand fetch"}),
        7);

    // Complete the instruction (fetch next opcode)
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Assert: A register = $40
    REQUIRE(machine.cpu().a == 0x40);

    // Assert: instruction completed at cycle 9 (7 reset + 2 instruction)
    REQUIRE(machine.cycle_count() == 9);

    // Assert: PC now past STA opcode at $D9CF
    REQUIRE(machine.cpu().pc.w == 0xD9D0);
}

// ===========================================================================
// Test: STA $0D00 writes RTI to NMI handler location
// ===========================================================================
// D9CF: 8D 00 0D  STA $0D00
// STA absolute takes 4 cycles:
//   Cycle 1: Fetch low byte of address
//   Cycle 2: Fetch high byte of address
//   Cycle 3: Write to target address
//   Cycle 4: Fetch next opcode

TEST_CASE("STA $0D00 writes RTI opcode", "[boot][deterministic][instruction]") {
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

    // Complete reset + first instruction (LDA #$40)
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }
    machine.step_instruction();  // Execute LDA #$40

    REQUIRE(machine.cpu().a == 0x40);
    REQUIRE(machine.cycle_count() == 9);

    // STA abs timing (4 cycles total):
    //   Cycle 9: Read low byte of address from $D9D0 (value $00)
    //   Cycle 10: Read high byte of address from $D9D1 (value $0D)
    //   Cycle 11: Write accumulator to target $0D00
    //   Cycle 12: Fetch next opcode from $D9D2

    // Assert: address low byte read at cycle 9
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0xD9D0, 0x00, false, "STA $0D00 address low byte"}),
        9);

    // Assert: address high byte read at cycle 10
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0xD9D1, 0x0D, false, "STA $0D00 address high byte"}),
        10);

    // Assert: write to $0D00 at EXACTLY cycle 11
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0x0D00, 0x40, true, "STA $0D00 - store RTI opcode"}),
        11);

    // Complete instruction (fetch next opcode)
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Assert: $0D00 now contains $40 (RTI opcode)
    REQUIRE(machine.read(0x0D00) == 0x40);
    REQUIRE(machine.cycle_count() == 13);
}

// ===========================================================================
// Test: SEI and CLD set processor flags
// ===========================================================================
// D9D2: 78        SEI    (2 cycles)
// D9D3: D8        CLD    (2 cycles)

TEST_CASE("SEI and CLD set processor flags", "[boot][deterministic][instruction]") {
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

    // Complete reset
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Execute LDA #$40 (cycles 7-8)
    machine.step_instruction();
    // Execute STA $0D00 (cycles 9-12)
    machine.step_instruction();

    REQUIRE(machine.cycle_count() == 13);

    // SEI (implied, 2 cycles):
    //   Cycle 13: Dummy read of next byte
    //   Cycle 14: Fetch next opcode (CLD at $D9D3)
    // The SEI opcode was fetched at cycle 12

    // Execute SEI
    machine.step_instruction();
    REQUIRE(machine.cpu().p.bits.i == 1);
    REQUIRE(machine.cycle_count() == 15);

    // CLD (implied, 2 cycles):
    //   Cycle 15: Dummy read of next byte
    //   Cycle 16: Fetch next opcode (LDX at $D9D4)

    // Execute CLD
    machine.step_instruction();
    REQUIRE(machine.cpu().p.bits.d == 0);
    REQUIRE(machine.cycle_count() == 17);
}

// ===========================================================================
// Test: LDX #$FF and TXS set up stack
// ===========================================================================
// D9D4: A2 FF     LDX #$FF   (2 cycles)
// D9D6: 9A        TXS        (2 cycles)

TEST_CASE("LDX #$FF and TXS set up stack", "[boot][deterministic][instruction]") {
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

    // Complete reset
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Execute first 4 instructions: LDA(2), STA(4), SEI(2), CLD(2) = 10 cycles
    // Plus reset(7) = 17 cycles total
    for (int i = 0; i < 4; ++i) {
        machine.step_instruction();
    }

    REQUIRE(machine.cycle_count() == 17);

    // LDX # (immediate, 2 cycles):
    //   Cycle 17: Read operand $FF from $D9D5
    //   Cycle 18: Fetch next opcode (TXS at $D9D6)

    // Assert: operand $FF read at EXACTLY cycle 17
    ASSERT_ACCESS_AT(machine,
        (ExpectedAccess{0xD9D5, 0xFF, false, "LDX #$FF operand fetch"}),
        17);

    // Complete LDX
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }
    REQUIRE(machine.cpu().x == 0xFF);
    REQUIRE(machine.cycle_count() == 19);

    // TXS (implied, 2 cycles):
    //   Cycle 19: Dummy read
    //   Cycle 20: Fetch next opcode

    // Execute TXS
    machine.step_instruction();
    REQUIRE(machine.cpu().s.b.l == 0xFF);
    REQUIRE(machine.cycle_count() == 21);

    // Assert: PC now past next opcode
    REQUIRE(machine.cpu().pc.w == 0xD9D8);
}

// ===========================================================================
// Test: Complete boot initialization sequence (first 6 instructions)
// ===========================================================================

TEST_CASE("Boot initialization sequence complete", "[boot][deterministic][sequence]") {
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

    // Track all writes
    std::vector<std::pair<uint16_t, uint8_t>> writes;
    machine.add_watchpoint(0x0000, 0x4000, WATCH_WRITE,
        [&](uint16_t addr, uint8_t val, bool, uint64_t) {
            writes.push_back({addr, val});
        });

    // Complete reset
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }

    // Execute exactly 6 instructions
    for (int i = 0; i < 6; ++i) {
        machine.step_instruction();
    }

    machine.clear_watchpoints();

    // Assert CPU state after 6 instructions
    REQUIRE(machine.cpu().a == 0x40);       // From LDA #$40
    REQUIRE(machine.cpu().x == 0xFF);       // From LDX #$FF
    REQUIRE(machine.cpu().s.b.l == 0xFF);   // From TXS
    REQUIRE(machine.cpu().p.bits.i == 1);   // From SEI
    REQUIRE(machine.cpu().p.bits.d == 0);   // From CLD

    // Assert memory state
    REQUIRE(machine.read(0x0D00) == 0x40);  // RTI stored at NMI handler

    // Assert we saw exactly one write (to $0D00)
    REQUIRE(writes.size() == 1);
    REQUIRE(writes[0].first == 0x0D00);
    REQUIRE(writes[0].second == 0x40);

    // Assert cycle count:
    // 7 (reset) + 2 (LDA) + 4 (STA abs) + 2 (SEI) + 2 (CLD) + 2 (LDX) + 2 (TXS) = 21
    REQUIRE(machine.cycle_count() == 21);
}

// ===========================================================================
// Unit tests for hardware components
// ===========================================================================

TEST_CASE("CRTC and Video ULA register access", "[boot][io]") {
    ModelB machine;

    SECTION("CRTC register access") {
        machine.write(0xFE00, 12);   // Select register 12
        machine.write(0xFE01, 0x30); // Write value
        REQUIRE(machine.memory().crtc.reg(12) == 0x30);
    }

    SECTION("Video ULA register access") {
        machine.write(0xFE20, 0x9C);
        REQUIRE(machine.memory().video_ula.control() == 0x9C);
    }
}

TEST_CASE("Addressable latch via System VIA", "[boot][latch]") {
    ModelB machine;
    machine.reset();

    // Set DDRB bits 0-3 as output
    machine.write(0xFE42, 0x0F);

    // Write to latch address 6 (CAPS LED), data = 1: 0b1110 = 0x0E
    machine.write(0xFE40, 0x0E);
    REQUIRE(machine.memory().addressable_latch.caps_lock_led() == true);

    // Write to latch address 6, data = 0: 0b0110 = 0x06
    machine.write(0xFE40, 0x06);
    REQUIRE(machine.memory().addressable_latch.caps_lock_led() == false);
}

TEST_CASE("ROM verification", "[boot][rom]") {
    if (!roms_available()) {
        SKIP("ROM files not available at " BEEBIUM_ROM_DIR);
    }

    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    ModelB machine;

    auto mos = load_rom(rom_dir / "OS12.ROM");
    auto basic = load_rom(rom_dir / "BASIC2.ROM");

    REQUIRE(mos.size() == 16384);
    REQUIRE(basic.size() == 16384);

    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());

    // Reset vector
    REQUIRE(machine.read(0xFFFC) == 0xCD);
    REQUIRE(machine.read(0xFFFD) == 0xD9);

    // First instruction: LDA #$40
    REQUIRE(machine.read(0xD9CD) == 0xA9);
    REQUIRE(machine.read(0xD9CE) == 0x40);
}

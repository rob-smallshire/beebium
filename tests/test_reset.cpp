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

// Tests for BBC Micro reset behavior.
//
// The BBC Micro has two reset signals:
//   RST (from NE555): Triggered by Break key OR power-up, resets CPU and most peripherals
//   RSTA (from RC network): Triggered ONLY on power-up, resets System VIA only
//
// MOS detects reset type by checking System VIA's IER at $FE4E:
//   - If IER shows interrupts disabled -> assumes power-on -> full initialization
//   - If IER shows interrupts enabled -> Break only -> warm reset
//
// OSBYTE &FD (location $28B) reports reset type:
//   0 = soft reset (Break)
//   2 = hard reset (Ctrl-Break or power-on)

#include <catch2/catch_test_macros.hpp>
#include "beebium/Machines.hpp"
#include <filesystem>
#include <fstream>
#include <vector>

using namespace beebium;

namespace {

// System VIA IER address
constexpr uint16_t SYSTEM_VIA_IER = 0xFE4E;

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
#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "acorn-mos_1_20.rom") &&
           std::filesystem::exists(rom_dir / "bbc-basic_2.rom");
#else
    return false;
#endif
}

bool bplus_roms_available() {
#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dir / "acorn-mos_2_0.rom") &&
           std::filesystem::exists(rom_dir / "bbc-basic_2.rom");
#else
    return false;
#endif
}

// Setup machine with ROMs
void setup_machine(ModelB& machine) {
#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dir / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
#endif
}

void setup_machine_bplus(ModelBPlus& machine) {
#ifdef BEEBIUM_ROM_DIR
    const auto rom_dir = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos = load_rom(rom_dir / "acorn-mos_2_0.rom");
    auto basic = load_rom(rom_dir / "bbc-basic_2.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
#endif
}

// Boot the machine until it reaches the BASIC prompt (or equivalent stable state)
// Returns the cycle count when boot completed
uint64_t boot_to_basic(ModelB& machine, uint64_t max_cycles = 10'000'000) {
    uint64_t cycles = 0;
    while (cycles < max_cycles) {
        machine.step();
        ++cycles;

        // Check if we've reached a stable state by looking for the cursor prompt
        // The machine is ready when MOS has initialized and is waiting for input
        // We can detect this by checking if the keyboard interrupt is enabled
        uint8_t ier = machine.peek(SYSTEM_VIA_IER);
        if ((ier & 0x81) == 0x81) {  // IRQ7 (bit 7) and CA2 (bit 0) enabled
            // Give it a bit more time to fully initialize
            for (int i = 0; i < 100000; ++i) {
                machine.step();
                ++cycles;
            }
            return cycles;
        }
    }
    return cycles;
}

// Helper to boot Model B Plus to BASIC
uint64_t boot_to_basic_bplus(ModelBPlus& machine, uint64_t max_cycles = 10'000'000) {
    uint64_t cycles = 0;
    while (cycles < max_cycles) {
        machine.step();
        ++cycles;

        // Check if keyboard interrupt is enabled (indicating MOS is ready)
        uint8_t ier = machine.peek(SYSTEM_VIA_IER);
        if ((ier & 0x81) == 0x81) {
            // Give it more time to fully initialize
            for (int i = 0; i < 100000; ++i) {
                machine.step();
                ++cycles;
            }
            return cycles;
        }
    }
    return cycles;
}

} // namespace

// =============================================================================
// Reset Type Detection Tests
// =============================================================================

TEST_CASE("Soft reset preserves System VIA interrupt state", "[reset][via]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine - this does a power-on reset
    boot_to_basic(machine);

    // After boot, System VIA IER should have interrupts enabled by MOS
    uint8_t ier_before = machine.peek(SYSTEM_VIA_IER);
    REQUIRE((ier_before & 0x7F) != 0);  // At least some interrupts enabled

    // Perform a soft reset (Break key)
    machine.soft_reset();

    // System VIA IER should be PRESERVED (not cleared)
    // This is how MOS detects soft vs hard reset
    uint8_t ier_after = machine.peek(SYSTEM_VIA_IER);
    CHECK(ier_after == ier_before);
}

TEST_CASE("Power-on reset clears System VIA and RAM", "[reset][via]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine
    boot_to_basic(machine);

    // Write a pattern to RAM to verify it gets cleared
    machine.write(0x0400, 0xAA);
    machine.write(0x0401, 0x55);

    // Verify the pattern is there
    REQUIRE(machine.peek(0x0400) == 0xAA);
    REQUIRE(machine.peek(0x0401) == 0x55);

    // Perform a power-on reset
    machine.reset();

    // System VIA IER should be cleared (bit 7 always reads as 1 per 6522 spec)
    uint8_t ier_after = machine.peek(SYSTEM_VIA_IER);
    CHECK((ier_after & 0x7F) == 0x00);  // Ignore bit 7, check bits 0-6

    // RAM should be cleared
    CHECK(machine.peek(0x0400) == 0x00);
    CHECK(machine.peek(0x0401) == 0x00);
}

TEST_CASE("Soft reset does not clear RAM", "[reset]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine
    boot_to_basic(machine);

    // Write a pattern to RAM
    machine.write(0x0400, 0xAA);
    machine.write(0x0401, 0x55);

    // Verify the pattern is there
    REQUIRE(machine.peek(0x0400) == 0xAA);
    REQUIRE(machine.peek(0x0401) == 0x55);

    // Perform a soft reset
    machine.soft_reset();

    // RAM should be PRESERVED
    CHECK(machine.peek(0x0400) == 0xAA);
    CHECK(machine.peek(0x0401) == 0x55);
}

// =============================================================================
// Software Hard Reset Trick Tests
// =============================================================================

TEST_CASE("Writing 127 to FE4E disables System VIA interrupts", "[reset][via]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine
    boot_to_basic(machine);

    // After boot, IER should have interrupts enabled
    uint8_t ier_before = machine.peek(SYSTEM_VIA_IER);
    REQUIRE((ier_before & 0x7F) != 0);

    // Write 127 (0x7F) to IER - this CLEARS bits 0-6 (bit 7 is clear/set flag)
    // Writing with bit 7 = 0 clears the specified bits
    machine.write(SYSTEM_VIA_IER, 0x7F);

    // IER should now be 0 (all interrupts disabled)
    // Note: Bit 7 always reads as 1 per 6522 spec
    uint8_t ier_after = machine.peek(SYSTEM_VIA_IER);
    CHECK((ier_after & 0x7F) == 0x00);
}

TEST_CASE("Software hard reset trick causes MOS to detect hard reset", "[reset][via][integration]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine fully to BASIC
    boot_to_basic(machine);

    // Verify we're in soft reset state (warm boot)
    // After a normal boot following power-on, MOS has configured the VIA
    uint8_t ier_before = machine.peek(SYSTEM_VIA_IER);
    REQUIRE((ier_before & 0x7F) != 0);  // Interrupts enabled

    // Apply the software hard reset trick:
    // Write 127 to $FE4E to disable all VIA interrupts
    machine.write(SYSTEM_VIA_IER, 0x7F);

    // Verify interrupts are now disabled (bit 7 always reads as 1 per 6522 spec)
    CHECK((machine.peek(SYSTEM_VIA_IER) & 0x7F) == 0x00);

    // Now perform a soft reset (simulating Break key)
    machine.soft_reset();

    // After soft reset, the VIA IER is still 0 (since soft reset preserves it)
    // When MOS runs, it will check IER and see it's unconfigured,
    // causing it to perform a full "hard reset" initialization
    CHECK((machine.peek(SYSTEM_VIA_IER) & 0x7F) == 0x00);

    // Boot the machine and let MOS detect the "hard reset"
    boot_to_basic(machine);

    // MOS should have detected the unconfigured VIA and performed full init
    // The VIA should now be configured again
    CHECK((machine.peek(SYSTEM_VIA_IER) & 0x7F) != 0);
}

// =============================================================================
// Break Key Timing Tests
// =============================================================================

TEST_CASE("Break down halts the CPU", "[reset][break]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine
    boot_to_basic(machine);

    // Press Break (hold reset line low)
    machine.break_down();

    // Verify we're in reset state
    CHECK(machine.is_in_reset());

    // Try to step the machine - CPU should be halted
    for (int i = 0; i < 1000; ++i) {
        machine.step();
    }

    // The CPU should have been halted
    // (The halted CPU may still cycle but won't execute real instructions)
    CHECK(machine.is_in_reset());
}

TEST_CASE("Break up triggers reset and resumes execution", "[reset][break]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine
    boot_to_basic(machine);

    // Press Break
    machine.break_down();
    CHECK(machine.is_in_reset());

    // Release Break
    machine.break_up();

    // Should no longer be in reset
    CHECK(!machine.is_in_reset());

    // Machine should now be able to run (will be in reset sequence)
    // The PC should be loaded from the reset vector
    // Boot again to verify the machine is operational
    uint64_t cycles = boot_to_basic(machine);
    CHECK(cycles > 0);  // Machine successfully booted
}

TEST_CASE("Break up performs soft reset", "[reset][break]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot machine
    boot_to_basic(machine);

    // Record System VIA IER
    uint8_t ier_before = machine.peek(SYSTEM_VIA_IER);
    REQUIRE((ier_before & 0x7F) != 0);

    // Press and release Break
    machine.break_down();
    machine.break_up();

    // System VIA IER should be preserved (soft reset)
    // Hardware always does a soft reset - MOS checks for Ctrl
    uint8_t ier_after = machine.peek(SYSTEM_VIA_IER);
    CHECK(ier_after == ier_before);
}

// =============================================================================
// Model B Plus Reset Tests
// =============================================================================

TEST_CASE("Model B Plus soft reset preserves System VIA", "[reset][via][b+]") {
    REQUIRE(bplus_roms_available());
    ModelBPlus machine;
    setup_machine_bplus(machine);

    // Boot machine
    boot_to_basic_bplus(machine);

    // After boot, System VIA IER should have interrupts enabled
    uint8_t ier_before = machine.peek(SYSTEM_VIA_IER);
    REQUIRE((ier_before & 0x7F) != 0);

    // Perform soft reset
    machine.soft_reset();

    // System VIA IER should be preserved
    CHECK(machine.peek(SYSTEM_VIA_IER) == ier_before);
}

// =============================================================================
// Break with a pending System VIA IRQ (issue #78)
// =============================================================================
//
// If the 6502 core's reset sequence fails to set the I flag, a Break taken
// while a System VIA interrupt is pending leaves reset through the IRQ vector
// (&FFFE) instead of the reset vector (&FFFC): the first instruction executed
// is the MOS IRQ handler, not the MOS reset code, so the machine never
// re-initialises and never records the Break. This reproduces the field
// failure from #78 at the machine level.

TEST_CASE("Break with a pending System VIA IRQ still enters the reset handler (#78)",
          "[reset][via][break][integration]") {
    REQUIRE(roms_available());
    ModelB machine;
    setup_machine(machine);

    // Boot fully so the MOS has enabled System VIA interrupts.
    boot_to_basic(machine);
    constexpr uint16_t SYSTEM_VIA_IFR = 0xFE4D;
    REQUIRE((machine.peek(SYSTEM_VIA_IER) & 0x7F) != 0);

    // Hijack the CPU into "SEI; JMP *" so interrupts are masked and the CPU
    // spins in RAM without ever servicing the VIA. Bytes at &0900:
    //   78          SEI
    //   4C 01 09    JMP &0901   (the JMP jumps to itself)
    machine.write(0x0900, 0x78);
    machine.write(0x0901, 0x4C);
    machine.write(0x0902, 0x01);
    machine.write(0x0903, 0x09);
    machine.set_pc(0x0900);

    // Run ~60 ms of emulated time (2 MHz -> 120000 cycles). With interrupts
    // masked the System VIA interrupt goes pending and stays pending.
    for (int i = 0; i < 120000; ++i) {
        machine.step();
    }

    // A System VIA interrupt is now pending (enabled and flagged), unserviced.
    uint8_t ier = machine.peek(SYSTEM_VIA_IER);
    uint8_t ifr = machine.peek(SYSTEM_VIA_IFR);
    INFO("IER=" << std::hex << int(ier) << " IFR=" << int(ifr));
    REQUIRE((ier & ifr & 0x7F) != 0);

    // Press and release Break (soft reset; no Ctrl held).
    machine.break_down();
    machine.break_up();
    REQUIRE(!machine.is_in_reset());

    // Step to the first opcode fetch out of reset.
    while (!M6502_IsAboutToExecute(&machine.cpu())) {
        machine.step();
    }
    uint16_t first_pc = machine.cpu().abus.w;
    uint16_t reset_vector =
        machine.peek(0xFFFC) | (uint16_t(machine.peek(0xFFFD)) << 8);
    INFO("first_pc=" << std::hex << first_pc << " reset_vector=" << reset_vector);
    CHECK(first_pc == reset_vector); // enters the reset handler, not the IRQ handler

    // Let the MOS reset code run (1 s emulated) and confirm it recorded a soft
    // Break at &028D (0 = soft Break; 1 = the power-on value it would keep if
    // the reset code never ran).
    for (int i = 0; i < 2'000'000; ++i) {
        machine.step();
    }
    CHECK(machine.peek(0x028D) == 0);
}

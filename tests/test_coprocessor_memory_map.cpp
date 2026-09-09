// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Tests for the 6502 second processor (cheese wedge) coprocessor memory map.
//
// The coprocessor memory map has two modes:
//   Boot mode: ROM overlays RAM at &F000-&FFFF for reads; writes pass through to RAM.
//              Tube registers at &FEF8-&FEFF override both ROM and RAM.
//   Normal mode: all RAM except Tube registers at &FEF8-&FEFF.
//
// Transition from boot to normal occurs on first Tube register access.
//
// Reference: 6502 Second Processor Service Manual, Sections 5.1-5.3.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <numeric>

using namespace beebium;

// Helper: fill ROM with a recognisable pattern (address low byte XOR 0xFF).
static std::array<uint8_t, 4096> make_test_rom() {
    std::array<uint8_t, 4096> rom{};
    for (size_t i = 0; i < rom.size(); ++i) {
        rom[i] = static_cast<uint8_t>(i ^ 0xFF);
    }
    return rom;
}

// ===========================================================================
// Construction and initial state
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap starts in boot mode", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    CHECK(mem.boot_mode());
}

// ===========================================================================
// RAM access (low memory, always RAM)
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap RAM read/write in low memory", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);

    mem.write(0x0000, 0x42);
    CHECK(mem.read(0x0000) == 0x42);

    mem.write(0x1234, 0xAB);
    CHECK(mem.read(0x1234) == 0xAB);

    mem.write(0xEFFF, 0xCD);
    CHECK(mem.read(0xEFFF) == 0xCD);
}

TEST_CASE("CoprocessorMemoryMap RAM initialised to zero", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);

    CHECK(mem.read(0x0000) == 0x00);
    CHECK(mem.read(0x8000) == 0x00);
    CHECK(mem.read(0xEFFF) == 0x00);
}

// ===========================================================================
// Boot mode: ROM overlay at &F000-&FFFF
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap boot mode: reads from ROM region return ROM data", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // The 4 KB ROM is mapped at &F000-&FFFF: machine address A -> rom[A & 0xFFF].
    // &F800 maps to ROM offset 0x800
    CHECK(mem.read(0xF800) == rom[0x800]);

    // &F900 maps to ROM offset 0x900
    CHECK(mem.read(0xF900) == rom[0x900]);

    // &FFFC (reset vector low) maps to ROM offset 0xFFC
    CHECK(mem.read(0xFFFC) == rom[0xFFC]);

    // &FFFD (reset vector high) maps to ROM offset 0xFFD
    CHECK(mem.read(0xFFFD) == rom[0xFFD]);

    // &FFFF maps to ROM offset 0xFFF
    CHECK(mem.read(0xFFFF) == rom[0xFFF]);
}

TEST_CASE("CoprocessorMemoryMap boot mode: lower half (&F000-&F7FF) is ROM", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // The lower 2 KB of the 2732 is genuine ROM address space (a client like
    // ReCo6502 executes from it). &F000 maps to ROM offset 0, &F7FF to 0x7FF.
    CHECK(mem.read(0xF000) == rom[0x000]);
    CHECK(mem.read(0xF7FF) == rom[0x7FF]);

    // Writes to the lower half still go to RAM (the ROM only shadows reads);
    // they become visible once boot mode ends.
    mem.write(0xF000, 0x11);
    mem.write(0xF7FF, 0x22);
    CHECK(mem.read(0xF000) == rom[0x000]);  // ROM still shadows the read
    CHECK(mem.read(0xF7FF) == rom[0x7FF]);

    mem.read(0xFEF8);  // leave boot mode
    REQUIRE_FALSE(mem.boot_mode());
    CHECK(mem.read(0xF000) == 0x11);  // now the RAM writes are visible
    CHECK(mem.read(0xF7FF) == 0x22);
}

TEST_CASE("CoprocessorMemoryMap boot mode: writes to ROM region go to RAM", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // Write to an address in the ROM region
    mem.write(0xF800, 0x42);

    // Reading should still return ROM data (ROM shadows RAM for reads)
    CHECK(mem.read(0xF800) == rom[0x800]);

    // After leaving boot mode, the RAM value should be visible
    // (We'll test this in the transition tests below)
}

TEST_CASE("CoprocessorMemoryMap boot mode: ROM copy pattern works", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // Simulate the boot ROM's self-copy: read from ROM, write back to same address.
    // In boot mode, reads come from ROM and writes go to RAM.
    for (uint16_t addr = 0xF000; addr != 0x0000; ++addr) {  // wraps at 0x10000
        // Skip Tube register range -- accessing those would end boot mode
        if (addr >= 0xFEF8 && addr <= 0xFEFF)
            continue;
        uint8_t byte = mem.read(addr);  // reads from ROM
        mem.write(addr, byte);          // writes to RAM
    }

    // Still in boot mode (we skipped Tube registers)
    REQUIRE(mem.boot_mode());

    // Force transition to normal mode by accessing a Tube register
    mem.read(0xFEF8);
    REQUIRE_FALSE(mem.boot_mode());

    // Now reads should return the copied ROM data from RAM
    CHECK(mem.read(0xF800) == rom[0x800]);
    CHECK(mem.read(0xFFFC) == rom[0xFFC]);
    CHECK(mem.read(0xFFFD) == rom[0xFFD]);
    CHECK(mem.read(0xFFFF) == rom[0xFFF]);
}

// ===========================================================================
// Boot mode: Tube registers override ROM
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap boot mode: Tube register read overrides ROM", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    // Pre-load a known value into R4 H-to-P via the host side
    tube.host_write(7, 0x99);

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // &FEFF = R4 data (offset 7). Reading should return Tube data, not ROM.
    uint8_t val = mem.read(0xFEFF);
    CHECK(val == 0x99);

    // Boot mode should now be terminated
    CHECK_FALSE(mem.boot_mode());
}

TEST_CASE("CoprocessorMemoryMap boot mode: Tube register write overrides RAM", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // Write to R4 data (&FEFF, offset 7). Should go to Tube, not RAM.
    mem.write(0xFEFF, 0x55);

    // Tube register should have the value (host can read R4 P-to-H)
    uint8_t r4_status = tube.host_read(6);
    CHECK((r4_status & TubeUla::DATA_AVAILABLE) != 0);
    CHECK(tube.host_read(7) == 0x55);

    // Boot mode should now be terminated
    CHECK_FALSE(mem.boot_mode());
}

// ===========================================================================
// Boot mode termination
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap boot mode terminated by Tube read", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // Read R1 status (&FEF8, offset 0) -- the typical first Tube access
    mem.read(0xFEF8);

    CHECK_FALSE(mem.boot_mode());
}

TEST_CASE("CoprocessorMemoryMap boot mode terminated by Tube write", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    // Write to R1 data (&FEF9, offset 1)
    mem.write(0xFEF9, 0x42);

    CHECK_FALSE(mem.boot_mode());
}

TEST_CASE("CoprocessorMemoryMap boot mode: all 8 Tube addresses terminate boot", "[coprocessor][memory][boot]") {
    for (uint16_t offset = 0; offset < 8; ++offset) {
        TubeUla tube;
        auto rom = make_test_rom();

        CoprocessorMemoryMap mem(tube, rom);
        REQUIRE(mem.boot_mode());

        mem.read(0xFEF8 + offset);
        CHECK_FALSE(mem.boot_mode());
    }
}

TEST_CASE("CoprocessorMemoryMap boot mode cannot be re-entered", "[coprocessor][memory][boot]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    mem.read(0xFEF8);  // terminate boot mode
    REQUIRE_FALSE(mem.boot_mode());

    // No way to re-enter boot mode (only reset can do that)
    // Verify ROM region now returns RAM data (zeroes)
    CHECK(mem.read(0xF800) == 0x00);
    CHECK(mem.read(0xFFFF) == 0x00);
}

// ===========================================================================
// Normal mode: all RAM except Tube registers
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap normal mode: ROM region reads from RAM", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);

    // Write to RAM while in boot mode
    mem.write(0xF800, 0xAA);
    mem.write(0xFF00, 0xBB);

    // Transition to normal mode
    mem.read(0xFEF8);
    REQUIRE_FALSE(mem.boot_mode());

    // Now reads from ROM region return RAM data
    CHECK(mem.read(0xF800) == 0xAA);
    CHECK(mem.read(0xFF00) == 0xBB);
}

TEST_CASE("CoprocessorMemoryMap normal mode: Tube registers still active", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    mem.read(0xFEF8);  // end boot mode
    REQUIRE_FALSE(mem.boot_mode());

    // Write to Tube and verify it goes to the Tube, not RAM
    mem.write(0xFEFF, 0x77);  // R4 data
    CHECK((tube.host_read(6) & TubeUla::DATA_AVAILABLE) != 0);
    CHECK(tube.host_read(7) == 0x77);
}

TEST_CASE("CoprocessorMemoryMap normal mode: RAM behind Tube registers is not accessible", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);

    // Write to Tube register addresses while in boot mode (goes to Tube, not RAM)
    // First write some values to RAM at those addresses by going through
    // a write that bypasses Tube... actually we can't. In both modes,
    // &FEF8-&FEFF always go to Tube registers. The RAM at those addresses
    // is unreachable, just like the real hardware where CAS is suppressed.
    // This test verifies that reads from Tube addresses return Tube data,
    // not whatever is in RAM.

    mem.read(0xFEF8);  // end boot mode

    // R1 status read should return Tube status, not RAM
    uint8_t status = mem.read(0xFEF8);
    // Should have control flags in bits 5-0 and status bits in 7-6
    // At minimum, bits 5-0 should be 0 (no flags set) and bit 6 should
    // be set (P-to-H FIFO not full = space available)
    CHECK((status & 0x40) != 0);  // space available
}

// ===========================================================================
// Reset
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap reset re-enters boot mode", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);
    mem.read(0xFEF8);  // end boot mode
    REQUIRE_FALSE(mem.boot_mode());

    mem.reset();
    CHECK(mem.boot_mode());

    // ROM should be visible again
    CHECK(mem.read(0xF800) == rom[0x800]);
}

TEST_CASE("CoprocessorMemoryMap reset preserves RAM contents", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);

    // Write to low RAM
    mem.write(0x1000, 0x42);

    mem.read(0xFEF8);  // end boot mode
    mem.reset();

    // RAM should still have the value (real DRAM retains contents across reset)
    CHECK(mem.read(0x1000) == 0x42);
}

// ===========================================================================
// Address mirroring: Tube registers are only 8 bytes
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap Tube register range is exactly &FEF8-&FEFF", "[coprocessor][memory]") {
    TubeUla tube;
    auto rom = make_test_rom();

    CoprocessorMemoryMap mem(tube, rom);

    // &FEF7 should be ROM in boot mode, not a Tube register
    uint8_t val = mem.read(0xFEF7);
    CHECK(val == rom[0xFEF7 - 0xF000]);

    // Should still be in boot mode (didn't access Tube range)
    CHECK(mem.boot_mode());
}

// ===========================================================================
// Reset vector accessible from ROM
// ===========================================================================

TEST_CASE("CoprocessorMemoryMap boot mode: reset vector readable from ROM", "[coprocessor][memory][boot]") {
    TubeUla tube;

    // Create a ROM with a known reset vector
    auto rom = make_test_rom();
    rom[0xFFC] = 0x00;  // reset vector low: &F800
    rom[0xFFD] = 0xF8;  // reset vector high

    CoprocessorMemoryMap mem(tube, rom);
    REQUIRE(mem.boot_mode());

    uint16_t reset_vector = mem.read(0xFFFC) | (mem.read(0xFFFD) << 8);
    CHECK(reset_vector == 0xF800);
}

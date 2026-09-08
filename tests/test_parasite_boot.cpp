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

// Integration tests for the 6502 second processor boot sequence.
//
// These tests load the real Acorn Tube 6502 Client ROM (v1.10, external
// "cheese wedge" second processor) and verify the full boot sequence:
//
//   1. CPU fetches reset vector from boot ROM (&F800)
//   2. ROM self-copy: four loops copy ROM contents to RAM, avoiding Tube
//      registers at &FEF8-&FEFF
//   3. 17-byte stub copied to &0100
//   4. JMP &0100 executes the stub, which reads Tube R1 status (&FEF8),
//      terminating boot mode (hardware D-type flip-flop clears)
//   5. CLI enables interrupts
//   6. PrText prints the banner "Acorn TUBE 6502 64K" via OSWRCH, which
//      writes characters to R1 P-to-H (&FEF9). The banner is exactly
//      24 bytes -- the full capacity of the R1 P-to-H FIFO
//   7. After the banner, the code patches JMP at &F85D to point to
//      CmdPrompt (&F88D) instead of the banner routine
//   8. JSR WaitByte polls R2 status (&FEFA) waiting for host acknowledge
//
// Without a host to respond, the parasite spins in the WaitByte loop.
//
// Reference: docs/disassemblies/6502-Tube-Client-v1.10.bas (J.G.Harston)
// Reference: 6502 Second Processor Service Manual, Sections 5.1-5.3

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/ParasiteRunner.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#ifndef BEEBIUM_ROM_DIR
#error "BEEBIUM_ROM_DIR must be defined"
#endif

using namespace beebium;

static constexpr const char* ROM_FILENAME = "acorn-tube-6502_1_10.rom";
static constexpr size_t ROM_SIZE = 2048;

// Load the ROM file into a fixed-size array.
static std::array<uint8_t, ROM_SIZE> load_rom() {
    auto filepath = std::filesystem::path(BEEBIUM_TUBE_ROM_DIR) / ROM_FILENAME;
    std::ifstream file(filepath, std::ios::binary);
    REQUIRE(file.good());

    std::array<uint8_t, ROM_SIZE> rom{};
    file.read(reinterpret_cast<char*>(rom.data()), ROM_SIZE);
    REQUIRE(file.gcount() == ROM_SIZE);
    return rom;
}

// Enough cycles for the boot copy loops to complete and the banner to be
// printed. The copy loops take ~22,000 cycles; the banner output (24
// OSWRCH calls, each polling then writing) takes a few hundred more.
// 100,000 cycles gives ample margin.
static constexpr uint64_t BOOT_CYCLES = 100000;

// ===========================================================================
// Phase 1: Reset vector and boot mode
// ===========================================================================

TEST_CASE("Parasite boot: reset vector points to F800", "[parasite][boot][rom]") {
    auto rom = load_rom();

    // Verify ROM reset vector
    uint16_t reset_vector = rom[0x7FC] | (rom[0x7FD] << 8);
    CHECK(reset_vector == 0xF800);
}

TEST_CASE("Parasite boot: starts in boot mode", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();

    CHECK(runner.memory_map().boot_mode());
}

TEST_CASE("Parasite boot: CPU begins at reset vector", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();

    // Execute reset sequence (7 cycles)
    runner.step_instruction();

    CHECK(runner.cpu().abus.w == 0xF800);
    CHECK(runner.memory_map().boot_mode());
}

// ===========================================================================
// Phase 2: ROM self-copy to RAM
// ===========================================================================

TEST_CASE("Parasite boot: ROM copied to RAM", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    auto& mem = runner.memory_map();

    // Boot mode must be terminated by now
    REQUIRE_FALSE(mem.boot_mode());

    // Check ROM copied to RAM at &F800-&FDFF (indirect copy loop)
    // Skip &F85E-&F85F which are patched by the boot code (JMP target rewritten
    // from banner display to CmdPrompt).
    for (uint16_t addr = 0xF800; addr <= 0xFDFF; ++addr) {
        if (addr == 0xF85E || addr == 0xF85F) continue;
        uint16_t rom_offset = addr - 0xF800;
        INFO("Address: 0x" << std::hex << addr);
        CHECK(mem.ram(addr) == rom[rom_offset]);
    }

    // Check ROM copied to RAM at &FE00-&FEEF (indexed copy loop,
    // avoids Tube registers at &FEF0+)
    for (uint16_t addr = 0xFE00; addr <= 0xFEEF; ++addr) {
        uint16_t rom_offset = addr - 0xF800;
        INFO("Address: 0x" << std::hex << addr);
        CHECK(mem.ram(addr) == rom[rom_offset]);
    }

    // Check ROM copied to RAM at &FF00-&FFFF (first copy loop)
    // Use uint32_t to avoid uint16_t overflow when addr wraps past 0xFFFF.
    for (uint32_t addr = 0xFF00; addr <= 0xFFFF; ++addr) {
        uint16_t rom_offset = static_cast<uint16_t>(addr) - 0xF800;
        INFO("Address: 0x" << std::hex << addr);
        CHECK(mem.ram(static_cast<uint16_t>(addr)) == rom[rom_offset]);
    }
}

TEST_CASE("Parasite boot: stub copied to page 1", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    auto& mem = runner.memory_map();

    // 17 bytes (indices 0-16) copied from ROM &F859 to RAM &0100
    for (int i = 0; i <= 0x10; ++i) {
        uint16_t rom_offset = 0xF859 - 0xF800 + i;
        INFO("Offset: 0x" << std::hex << i);
        CHECK(mem.ram(0x0100 + i) == rom[rom_offset]);
    }
}

// ===========================================================================
// Phase 3: Boot mode termination
// ===========================================================================

TEST_CASE("Parasite boot: boot mode terminated by Tube register access", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // The stub at &0100 executes LDA &FEF8 (Tube R1 status), which
    // terminates boot mode.
    CHECK_FALSE(runner.memory_map().boot_mode());
}

// ===========================================================================
// Phase 4: Banner printed to R1 P-to-H FIFO
// ===========================================================================

TEST_CASE("Parasite boot: banner written to R1 P-to-H FIFO", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // The v1.10 PrText routine calls OSWRCH for each byte < 0x80 in the
    // inline string. The banner is:
    //   LF "Acorn TUBE 6502 64K" LF LF CR NUL
    // That's exactly 24 bytes -- the full R1 P-to-H FIFO capacity.
    static constexpr uint8_t expected_banner[] = {
        0x0A,                                           // LF
        'A', 'c', 'o', 'r', 'n', ' ',                  // "Acorn "
        'T', 'U', 'B', 'E', ' ',                        // "TUBE "
        '6', '5', '0', '2', ' ',                        // "6502 "
        '6', '4', 'K',                                  // "64K"
        0x0A, 0x0A, 0x0D, 0x00                          // LF LF CR NUL
    };
    static_assert(sizeof(expected_banner) == 24);

    // Read the banner from the host side of the TubeUla
    uint8_t status = tube.host_read(0);
    CHECK((status & TubeUla::DATA_AVAILABLE) != 0);

    for (int i = 0; i < 24; ++i) {
        uint8_t byte = tube.host_read(1);
        INFO("FIFO position: " << i
             << " expected: 0x" << std::hex << (int)expected_banner[i]
             << " got: 0x" << (int)byte);
        CHECK(byte == expected_banner[i]);
    }
}

// ===========================================================================
// Phase 5: JMP patched for subsequent resets
// ===========================================================================

TEST_CASE("Parasite boot: JMP at F85D patched to CmdPrompt", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    auto& mem = runner.memory_map();

    // After printing the banner, the code patches the JMP at &F85D:
    //   LDA #&8D; STA &F85E   -- low byte of CmdPrompt (&F88D)
    //   LDA #&F8; STA &F85F   -- high byte
    // Original JMP target was &F860 (banner display).
    // After patch, target is &F88D (CmdPrompt, which prints "*" prompt).
    CHECK(mem.ram(0xF85E) == 0x8D);  // low byte of CmdPrompt
    CHECK(mem.ram(0xF85F) == 0xF8);  // high byte of CmdPrompt
}

// ===========================================================================
// Phase 6: CPU polling WaitByte for host acknowledge
// ===========================================================================

TEST_CASE("Parasite boot: CPU spins in WaitByte polling R2 status", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    // After the banner and JMP patch, the code calls:
    //   JSR WaitByte
    // WaitByte is at &F975:
    //   BIT &FEFA   ; R2 status -- bit 7 = data available
    //   BPL &F975   ; loop until data available
    //   LDA &FEFB   ; read R2 data
    //   RTS
    //
    // Without a host providing R2 data, the CPU spins in the
    // BIT/BPL loop. The PC oscillates between &F975 and &F978.
    uint16_t pc = runner.cpu().abus.w;
    bool in_wait_byte = (pc >= 0xF975 && pc <= 0xF979);
    CHECK(in_wait_byte);
}

// ===========================================================================
// Phase 7: Vector table installed correctly
// ===========================================================================

TEST_CASE("Parasite boot: MOS vectors installed at 0200", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    auto& mem = runner.memory_map();

    // The boot code copies 0x37 bytes from ROM &FF80 to RAM &0200.
    // This installs the MOS vector table (USERV, BRKV, IRQ1V, ...).
    // Check a few key vectors:

    // WRCHV at &020E/&020F should point to osWRCH (&F962)
    uint16_t wrchv = mem.ram(0x020E) | (mem.ram(0x020F) << 8);
    CHECK(wrchv == 0xF962);

    // BRKV at &0202/&0203 should point to ErrorHandler
    // (the boot code sets this up later, but the initial vector table
    // provides a default)
    uint16_t brkv = mem.ram(0x0202) | (mem.ram(0x0203) << 8);
    CHECK(brkv != 0x0000);  // Should be a valid address, not null

    // IRQ1V at &0204/&0205
    uint16_t irq1v = mem.ram(0x0204) | (mem.ram(0x0205) << 8);
    CHECK(irq1v != 0x0000);
}

// ===========================================================================
// Phase 8: Zero-page state after boot
// ===========================================================================

TEST_CASE("Parasite boot: zero-page state after boot", "[parasite][boot][rom]") {
    auto rom = load_rom();
    TubeUla tube;

    ParasiteRunner runner(tube, rom);
    runner.reset();
    runner.run(BOOT_CYCLES);

    auto& mem = runner.memory_map();

    // Escape flag at &FF should be cleared
    CHECK(mem.ram(0x00FF) == 0x00);

    // MEMTOP at &F2/&F3 should be &F800 (start of ROM code area)
    CHECK(mem.ram(0x00F2) == 0x00);
    CHECK(mem.ram(0x00F3) == 0xF8);
}

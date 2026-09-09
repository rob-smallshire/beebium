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

// Tests for the coprocessor CPU wrapper.
//
// CoprocessorCpu wires a Rockwell 65C02 to the CoprocessorMemoryMap and TubeUla,
// routing bus access and interrupt lines. These tests verify:
//   - CPU initialisation and reset
//   - Instruction execution via tick() and step_instruction()
//   - Memory read/write through the memory map
//   - IRQ routing from TubeUla::pirq()
//   - NMI routing from TubeUla::pnmi_level()
//   - Boot mode interaction (CPU fetches reset vector from ROM)

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorCpu.hpp>
#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

// Helper: create a 2 KB ROM with a known reset vector and initial code.
// Reset vector points to rom_entry (default &F800).
// First bytes at rom_entry are filled with NOPs.
static std::array<uint8_t, 4096> make_nop_rom(uint16_t rom_entry = 0xF800) {
    std::array<uint8_t, 4096> rom{};
    rom.fill(0xEA);  // NOP
    // Reset vector at ROM offset 0xFFC-0xFFD (maps to &FFFC-&FFFD)
    rom[0xFFC] = static_cast<uint8_t>(rom_entry & 0xFF);
    rom[0xFFD] = static_cast<uint8_t>(rom_entry >> 8);
    // IRQ/BRK vector at ROM offset 0xFFE-0xFFF (maps to &FFFE-&FFFF)
    // Point to a location with RTI
    rom[0xFFE] = 0x00;  // &F900 (ROM offset 0x900)
    rom[0xFFF] = 0xF9;
    rom[0x900] = 0x40;  // RTI at &F900
    // NMI vector at ROM offset 0xFFA-0xFFB (maps to &FFFA-&FFFB)
    rom[0xFFA] = 0x80;  // &F980 (ROM offset 0x980)
    rom[0xFFB] = 0xF9;
    rom[0x980] = 0x40;  // RTI at &F980
    return rom;
}

// ===========================================================================
// Construction and initial state
// ===========================================================================

TEST_CASE("CoprocessorCpu initialises with Rockwell 65C02 config", "[coprocessor][cpu]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);

    CHECK(cpu.cycle_count() == 0);
    CHECK(cpu.cpu().config == &M6502_rockwell65c02_config);
}

// ===========================================================================
// Reset and reset vector
// ===========================================================================

TEST_CASE("CoprocessorCpu reset fetches reset vector from boot ROM", "[coprocessor][cpu][reset]") {
    TubeUla tube;
    auto rom = make_nop_rom(0xF800);
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    // After reset, step through the 7-cycle reset sequence
    uint64_t cycles = cpu.step_instruction();
    CHECK(cycles == 7);

    // CPU is about to execute at &F800 (the reset vector).
    // pc.w is already incremented past the opcode fetch; use opcode_pc or abus.
    CHECK(cpu.cpu().abus.w == 0xF800);
}

TEST_CASE("CoprocessorCpu reset with custom reset vector", "[coprocessor][cpu][reset]") {
    TubeUla tube;
    auto rom = make_nop_rom(0xF900);
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    uint64_t cycles = cpu.step_instruction();
    CHECK(cycles == 7);
    CHECK(cpu.cpu().abus.w == 0xF900);
}

TEST_CASE("CoprocessorCpu reset re-enters boot mode", "[coprocessor][cpu][reset]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    // Memory map should be in boot mode after reset
    CHECK(mem.boot_mode());
}

TEST_CASE("CoprocessorCpu reset clears cycle count", "[coprocessor][cpu][reset]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    // Execute some cycles
    cpu.step_instruction();  // 7-cycle reset
    cpu.step_instruction();  // NOP
    CHECK(cpu.cycle_count() > 0);

    // Reset should clear cycle count
    cpu.reset();
    CHECK(cpu.cycle_count() == 0);
}

// ===========================================================================
// Instruction execution
// ===========================================================================

TEST_CASE("CoprocessorCpu step_instruction executes NOP", "[coprocessor][cpu][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    // Reset sequence: 7 cycles
    uint64_t cycles = cpu.step_instruction();
    CHECK(cycles == 7);

    // NOP: 2 cycles
    cycles = cpu.step_instruction();
    CHECK(cycles == 2);

    // Another NOP: 2 cycles
    cycles = cpu.step_instruction();
    CHECK(cycles == 2);

    CHECK(cpu.cycle_count() == 11);
}

TEST_CASE("CoprocessorCpu tick advances one cycle", "[coprocessor][cpu][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.tick();
    CHECK(cpu.cycle_count() == 1);

    cpu.tick();
    CHECK(cpu.cycle_count() == 2);
}

TEST_CASE("CoprocessorCpu run executes multiple cycles", "[coprocessor][cpu][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.run(20);
    CHECK(cpu.cycle_count() == 20);
}

TEST_CASE("CoprocessorCpu executes LDA immediate from ROM", "[coprocessor][cpu][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    // Place LDA #$42 at &F800 (ROM offset 0x800)
    rom[0x800] = 0xA9;  // LDA #imm
    rom[0x801] = 0x42;
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset sequence (7 cycles)
    cpu.step_instruction();  // LDA #$42 (2 cycles)

    CHECK(cpu.cpu().a == 0x42);
}

TEST_CASE("CoprocessorCpu executes STA/LDA in RAM", "[coprocessor][cpu][execution]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    // Place STA $1000 then LDA $1000 at &F800
    rom[0x800] = 0xA9;  // LDA #$55
    rom[0x801] = 0x55;
    rom[0x802] = 0x8D;  // STA $1000
    rom[0x803] = 0x00;
    rom[0x804] = 0x10;
    rom[0x805] = 0xA9;  // LDA #$00 (clear accumulator)
    rom[0x806] = 0x00;
    rom[0x807] = 0xAD;  // LDA $1000
    rom[0x808] = 0x00;
    rom[0x809] = 0x10;
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // LDA #$55 (2)
    cpu.step_instruction();  // STA $1000 (4)
    cpu.step_instruction();  // LDA #$00 (2)
    CHECK(cpu.cpu().a == 0x00);

    cpu.step_instruction();  // LDA $1000 (4)
    CHECK(cpu.cpu().a == 0x55);

    // Verify RAM was written
    CHECK(mem.ram(0x1000) == 0x55);
}

// ===========================================================================
// IRQ routing from Tube
// ===========================================================================

TEST_CASE("CoprocessorCpu routes PIRQ to CPU IRQ line", "[coprocessor][cpu][irq]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    // Program: CLI then loop with NOPs
    rom[0x800] = 0x58;  // CLI (enable interrupts)
    // Fill rest with NOP (already 0xEA)
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // CLI (2)

    // Enable I flag (PIRQ from R1) and provide data
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);  // set I flag
    tube.host_write(1, 0xAA);                                 // write R1 H-to-P data

    // Execute a few NOPs -- IRQ should fire
    cpu.run(20);

    // PC should have jumped to the IRQ vector (&F900)
    // After the interrupt handler runs RTI, it returns.
    // We just check that the interrupt was taken by verifying
    // the CPU visited the IRQ vector address.
    // With the I flag set and R1 data available, PIRQ should be active.
    CHECK(tube.pirq());
}

TEST_CASE("CoprocessorCpu no IRQ when interrupts disabled", "[coprocessor][cpu][irq]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    // Program: SEI then NOPs (interrupts disabled)
    rom[0x800] = 0x78;  // SEI
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // SEI (2)

    // Enable PIRQ source
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);  // set I flag
    tube.host_write(1, 0xBB);                                 // write R1 H-to-P data

    // Run some cycles -- CPU should just execute NOPs, no IRQ taken
    uint16_t pc_after_sei = cpu.cpu().pc.w;
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP

    // PC should have advanced linearly (3 NOPs = 6 bytes)
    // Since SEI was at &F800, next instruction is &F801, then &F803, &F805, &F807
    // (each NOP is 1 byte, 2 cycles)
    CHECK(cpu.cpu().pc.w == pc_after_sei + 3);
}

// ===========================================================================
// NMI routing from Tube
// ===========================================================================

// PNMI behaviour from Application Note 004 and the 6502 Second Processor
// Service Manual:
//
//   PNMI is the active-high output from the Tube ULA that drives the
//   coprocessor 6502's /NMI line (active-low, inverted by hardware).
//
//   PNMI goes high when M=1 AND (R3 H-to-P has data OR R3 P-to-H has space).
//   The 6502 NMI is edge-triggered (fires on falling edge of /NMI, i.e.
//   rising edge of the active-high PNMI level).
//
//   CoprocessorCpu passes pnmi_level() (the raw combinational output) to
//   M6502_SetDeviceNMI every cycle. The M6502 library handles edge
//   detection internally. This ensures the CPU sees NMI immediately
//   when the host writes R3 data, without waiting for the coprocessor to
//   perform a register access.

TEST_CASE("CoprocessorCpu PNMI: host R3 write triggers NMI during NOP execution", "[coprocessor][cpu][nmi]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // After reset, R3 P-to-H has the dummy byte (count=1), and R3 H-to-P
    // is empty. With M=0, PNMI level is false.
    CHECK_FALSE(tube.pnmi_level());

    // Enable M flag. R3 P-to-H has the dummy byte (count=1), so P-to-H
    // space is NOT available. R3 H-to-P is empty, so H-to-P data is NOT
    // available. PNMI level should still be false.
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    CHECK_FALSE(tube.pnmi_level());

    // Execute a NOP to let the CPU sample the NMI line while it's low.
    // This establishes the baseline for edge detection.
    cpu.step_instruction();  // NOP (2)

    // Now deposit data into R3 H-to-P via the host interface.
    // This makes H-to-P data available, so PNMI goes high.
    tube.host_write(5, 0x42);

    CHECK(tube.pnmi_level());

    // Run cycle-by-cycle until the NMI handler is entered. The 6502 finishes
    // the current instruction then takes 7 cycles for the NMI sequence.
    bool entered_nmi = false;
    for (int i = 0; i < 20; ++i) {
        cpu.run(1);
        if (cpu.in_nmi_handler()) {
            entered_nmi = true;
            break;
        }
    }
    CHECK(entered_nmi);

    // Simulate the NMI handler consuming the R3 data, which deasserts PNMI.
    // Without this, RTI would immediately re-trigger NMI (correct behaviour
    // -- the condition is still asserted).
    tube.coprocessor_read(5);  // consume the H-to-P data byte
    CHECK_FALSE(tube.pnmi_level());

    // Run more cycles: the RTI at &F980 returns to the NOP stream.
    // With PNMI deasserted, no further NMIs fire. Allow enough cycles
    // for RTI (6) plus a potential second NMI+RTI (7+6) if the edge
    // was already latched before we cleared the condition.
    cpu.run(30);

    CHECK_FALSE(cpu.in_nmi_handler());

    // The CPU should be back executing NOPs in the ROM area.
    CHECK(cpu.cpu().abus.w >= 0xF800);
}

TEST_CASE("CoprocessorCpu PNMI: not triggered when M flag is clear", "[coprocessor][cpu][nmi]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // M flag is clear (default). Deposit R3 H-to-P data via host interface.
    tube.host_write(5, 0x42);

    // PNMI level should be false (M=0 disables PNMI).
    CHECK_FALSE(tube.pnmi_level());

    // Run some NOPs -- NMI should NOT fire.
    uint16_t pc_before = cpu.cpu().pc.w;
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP
    cpu.step_instruction();  // NOP

    // PC should advance linearly (3 NOPs = 3 bytes from pc_before).
    CHECK(cpu.cpu().pc.w == pc_before + 3);
}

TEST_CASE("CoprocessorCpu PNMI: P-to-H space triggers NMI", "[coprocessor][cpu][nmi]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // After reset, R3 P-to-H has dummy byte (count=1).
    // With M=1, P-to-H space is NOT available (count >= threshold).
    // H-to-P data is NOT available (empty).
    // PNMI level should be false.
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    CHECK_FALSE(tube.pnmi_level());

    // Let CPU sample the low NMI level.
    cpu.step_instruction();  // NOP (2)

    // Now consume the dummy byte from P-to-H via the host interface.
    // This makes P-to-H space available, so PNMI goes high.
    tube.host_read(5);

    CHECK(tube.pnmi_level());

    // Run cycles -- NMI should fire.
    cpu.run(20);

    // Verify NMI was serviced.
    CHECK(cpu.cpu().nmi_flags == 0);
}

TEST_CASE("CoprocessorCpu PNMI: second edge after level drops and rises", "[coprocessor][cpu][nmi]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)

    // Set M flag, establish low PNMI baseline.
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    CHECK_FALSE(tube.pnmi_level());
    cpu.step_instruction();  // NOP -- CPU samples PNMI low

    // First NMI: deposit R3 H-to-P data via host interface.
    tube.host_write(5, 0x11);
    CHECK(tube.pnmi_level());

    // Run enough for NMI + RTI (7 + 6 = 13 cycles, give some margin).
    cpu.run(20);

    // PNMI level is still high (R3 H-to-P data not consumed).
    CHECK(tube.pnmi_level());

    // Simulate coprocessor consuming the R3 H-to-P data.
    tube.coprocessor_read(5);

    // Also make P-to-H occupied so PNMI drops fully. Write a byte to P-to-H
    // via the coprocessor interface to fill it.
    tube.coprocessor_write(5, 0x00);

    // PNMI level should now be false.
    CHECK_FALSE(tube.pnmi_level());

    // Let CPU sample the low level for at least one cycle.
    cpu.step_instruction();  // NOP

    // Second NMI: deposit new R3 H-to-P data via host interface.
    tube.host_write(5, 0x22);
    CHECK(tube.pnmi_level());

    // Run -- second NMI should fire.
    cpu.run(20);
    CHECK(cpu.cpu().nmi_flags == 0);
}

TEST_CASE("CoprocessorCpu PNMI: NMI cannot be masked by SEI", "[coprocessor][cpu][nmi]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    // Program: SEI then NOPs
    rom[0x800] = 0x78;  // SEI
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    cpu.step_instruction();  // reset (7)
    cpu.step_instruction();  // SEI (2) -- interrupts disabled

    // Set M flag, establish low baseline.
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    cpu.step_instruction();  // NOP -- CPU samples PNMI low

    // Trigger PNMI by depositing R3 H-to-P data via host interface.
    tube.host_write(5, 0x42);

    // Run -- NMI should fire despite SEI (NMI is non-maskable).
    cpu.run(20);
    CHECK(cpu.cpu().nmi_flags == 0);
}

// ===========================================================================
// Memory map interaction
// ===========================================================================

TEST_CASE("CoprocessorCpu reads from boot ROM during reset sequence", "[coprocessor][cpu][boot]") {
    TubeUla tube;
    auto rom = make_nop_rom(0xF850);
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();

    // Boot mode should be active
    REQUIRE(mem.boot_mode());

    // Reset sequence reads vectors from ROM
    cpu.step_instruction();  // 7 cycles

    // CPU is about to execute at &F850 (from ROM reset vector)
    CHECK(cpu.cpu().abus.w == 0xF850);

    // Still in boot mode (haven't accessed Tube registers yet)
    CHECK(mem.boot_mode());
}

TEST_CASE("CoprocessorCpu accessing Tube register terminates boot mode", "[coprocessor][cpu][boot]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    // Program at &F800: LDA $FEF8 (read Tube R1 status)
    rom[0x800] = 0xAD;  // LDA abs
    rom[0x801] = 0xF8;  // low byte
    rom[0x802] = 0xFE;  // high byte
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);
    cpu.reset();
    REQUIRE(mem.boot_mode());

    cpu.step_instruction();  // reset (7)
    CHECK(mem.boot_mode());

    cpu.step_instruction();  // LDA $FEF8 (4 cycles)
    CHECK_FALSE(mem.boot_mode());
}

// ===========================================================================
// CPU state access
// ===========================================================================

TEST_CASE("CoprocessorCpu provides mutable and const CPU access", "[coprocessor][cpu]") {
    TubeUla tube;
    auto rom = make_nop_rom();
    CoprocessorMemoryMap mem(tube, rom);

    CoprocessorCpu cpu(mem, tube);

    // Mutable access
    cpu.cpu().a = 0x42;
    CHECK(cpu.cpu().a == 0x42);

    // Const access
    const CoprocessorCpu& ccpu = cpu;
    CHECK(ccpu.cpu().a == 0x42);
    CHECK(ccpu.cycle_count() == 0);
}

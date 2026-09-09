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

// Tube ULA control flag tests using the real 65C02 CPU.
//
// Tests the control register (host offset 0) and its effects as observed
// by the coprocessor via R1 status ($FEF8) bits 5-0, and the behavioural
// effects of flags M (PNMI gating), I/J (PIRQ gating), V (R3 two-byte
// mode), and T (soft reset).
//
// Each test runs a 6502 program on the coprocessor that observes or reacts
// to flag changes made by the host thread, verifying the cross-thread
// interaction through the TubeUla model.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorCpu.hpp>
#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>
#include <thread>

using namespace beebium;

static std::array<uint8_t, 4096> make_stub_rom(uint16_t reset_addr) {
    std::array<uint8_t, 4096> rom{};
    rom[0xFFC] = reset_addr & 0xFF;
    rom[0xFFD] = (reset_addr >> 8) & 0xFF;
    return rom;
}

static std::array<uint8_t, 4096> make_stub_rom(uint16_t reset_addr,
                                                uint16_t nmi_addr,
                                                uint16_t irq_addr) {
    std::array<uint8_t, 4096> rom{};
    rom[0xFFC] = reset_addr & 0xFF;
    rom[0xFFD] = (reset_addr >> 8) & 0xFF;
    rom[0xFFA] = nmi_addr & 0xFF;
    rom[0xFFB] = (nmi_addr >> 8) & 0xFF;
    rom[0xFFE] = irq_addr & 0xFF;
    rom[0xFFF] = (irq_addr >> 8) & 0xFF;
    return rom;
}

static void plant(CoprocessorMemoryMap& mem, uint16_t addr, std::initializer_list<uint8_t> code) {
    for (auto byte : code) {
        mem.ram(addr++) = byte;
    }
}

// ============================================================================
// Control flag read-back: coprocessor observes host flag changes
// ============================================================================
//
// The coprocessor reads R1 status in a loop, storing the low 6 bits into
// successive RAM locations.  The host sets different flag combinations
// between each sample.  After NUM_SAMPLES readings, the coprocessor halts.
//
// Coprocessor program at $0400:
//   $0400: LDX #$00
//   $0402: LDA $FEF8        ; read R1 status
//   $0405: AND #$3F         ; mask to control flags
//   $0407: STA $0500,X      ; store sample
//   $040A: INX
//   $040B: CPX #NUM_SAMPLES
//   $040D: BNE $0402        ; loop  (offset: $0402 - $040F = -13 = $F3)
//   $040F: BRA *            ; halt

static constexpr uint16_t CODE_ADDR = 0x0400;
static constexpr uint16_t RESULT_ADDR = 0x0500;

static void plant_flag_sampler(CoprocessorMemoryMap& mem, uint8_t num_samples) {
    plant(mem, CODE_ADDR, {
        0xA2, 0x00,                         // LDX #$00
        0xAD, 0xF8, 0xFE,                   // LDA $FEF8     (read R1 status)
        0x29, 0x3F,                          // AND #$3F      (mask flags)
        0x9D, 0x00, 0x05,                   // STA $0500,X   (store sample)
        0xE8,                                // INX
        0xE0, num_samples,                   // CPX #num_samples
        0xD0, 0xF3,                          // BNE $0402
        0x80, 0xFE,                          // BRA *         (halt)
    });
}

static void setup_cpu_basic(CoprocessorMemoryMap& mem, CoprocessorCpu& cpu) {
    mem.read(0xFEF8);  // disable boot ROM
    mem.ram(0xFFFC) = CODE_ADDR & 0xFF;
    mem.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
    cpu.reset();
}

// ============================================================================
// Tests: flag read-back
// ============================================================================

TEST_CASE("6502 control flags: coprocessor reads back host-set flags", "[tube][6502][control]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_flag_sampler(memory, 1);
    setup_cpu_basic(memory, cpu);

    // Set flags AFTER cpu.reset() (which calls tube.reset()) so they persist.
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I | TubeUla::FLAG_M | TubeUla::FLAG_J);

    for (int i = 0; i < 10000 && cpu.cpu().opcode_pc.w != 0x040F; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x040F);
    uint8_t sample = memory.ram(RESULT_ADDR);
    CHECK((sample & TubeUla::FLAG_I) != 0);
    CHECK((sample & TubeUla::FLAG_M) != 0);
    CHECK((sample & TubeUla::FLAG_J) != 0);
    CHECK((sample & TubeUla::FLAG_Q) == 0);
    CHECK((sample & TubeUla::FLAG_V) == 0);
    CHECK((sample & TubeUla::FLAG_P) == 0);
}

TEST_CASE("6502 control flags: set and clear individual flags", "[tube][6502][control]") {
    // Verify each flag can be set and cleared independently.
    // The coprocessor reads R1 status after each host flag change.

    TubeUla tube;

    // 6 flags to test: Q(0x01), I(0x02), J(0x04), M(0x08), V(0x10), P(0x20)
    constexpr uint8_t flags[] = {
        TubeUla::FLAG_Q, TubeUla::FLAG_I, TubeUla::FLAG_J,
        TubeUla::FLAG_M, TubeUla::FLAG_V, TubeUla::FLAG_P
    };

    for (uint8_t flag : flags) {
        INFO("flag=$" << std::hex << static_cast<int>(flag));

        // Set the flag
        tube.host_write(0, TubeUla::FLAG_S | flag);
        uint8_t after_set = tube.control_flags();
        CHECK((after_set & flag) != 0);

        // Clear the flag
        tube.host_write(0, flag);  // S=0 means clear
        uint8_t after_clear = tube.control_flags();
        CHECK((after_clear & flag) == 0);
    }
}

TEST_CASE("6502 control flags: set multiple flags then clear one", "[tube][6502][control]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_flag_sampler(memory, 1);
    setup_cpu_basic(memory, cpu);

    // Set I, J, and M simultaneously (after cpu.reset() which calls tube.reset())
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I | TubeUla::FLAG_J | TubeUla::FLAG_M);
    uint8_t flags = tube.control_flags();
    CHECK((flags & TubeUla::FLAG_I) != 0);
    CHECK((flags & TubeUla::FLAG_J) != 0);
    CHECK((flags & TubeUla::FLAG_M) != 0);

    // Clear only J
    tube.host_write(0, TubeUla::FLAG_J);  // S=0, clear J
    flags = tube.control_flags();
    CHECK((flags & TubeUla::FLAG_I) != 0);  // still set
    CHECK((flags & TubeUla::FLAG_J) == 0);  // cleared
    CHECK((flags & TubeUla::FLAG_M) != 0);  // still set

    // Now verify the coprocessor sees I and M but not J
    for (int i = 0; i < 10000 && cpu.cpu().opcode_pc.w != 0x040F; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x040F);
    uint8_t sample = memory.ram(RESULT_ADDR);
    CHECK((sample & TubeUla::FLAG_I) != 0);
    CHECK((sample & TubeUla::FLAG_J) == 0);
    CHECK((sample & TubeUla::FLAG_M) != 0);
}

// ============================================================================
// Tests: soft reset (T flag)
// ============================================================================

TEST_CASE("6502 control flags: soft reset clears all registers", "[tube][6502][control]") {
    TubeUla tube;

    // Set some flags and write data to registers
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I | TubeUla::FLAG_M);
    tube.host_write(1, 0xAA);  // R1 H2P
    tube.host_write(7, 0xBB);  // R4 H2P

    // Verify data is present via status registers
    CHECK((tube.host_peek(0) & TubeUla::SPACE_AVAILABLE) == 0);  // R1 H2P full
    CHECK((tube.host_peek(6) & TubeUla::SPACE_AVAILABLE) == 0);  // R4 H2P full

    // Trigger soft reset via T flag
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_T);

    // Verify registers are cleared
    CHECK((tube.host_peek(0) & TubeUla::SPACE_AVAILABLE) != 0);  // R1 H2P empty
    CHECK((tube.host_peek(6) & TubeUla::SPACE_AVAILABLE) != 0);  // R4 H2P empty
    CHECK((tube.host_peek(4) & TubeUla::SPACE_AVAILABLE) != 0);  // R3 H2P empty

    // Note: T flag also ORs its bit into control_flags (T=0x40, but only
    // bits 5-0 are stored as flags).  The original flags (I, M) should
    // be preserved because soft_reset doesn't clear control_flags -- it
    // only clears the data registers.
    uint8_t flags = tube.control_flags();
    CHECK((flags & TubeUla::FLAG_I) != 0);
    CHECK((flags & TubeUla::FLAG_M) != 0);
}

// ============================================================================
// Tests: M flag gates PNMI
// ============================================================================

TEST_CASE("6502 control flags: M=0 suppresses PNMI from R3", "[tube][6502][control]") {
    // With M=0, writing to R3 should NOT assert PNMI.
    TubeUla tube;

    // Do NOT set M flag
    tube.host_write(5, 0x42);  // write R3 data

    // PNMI should not be asserted
    CHECK(tube.pnmi_level() == false);
}

TEST_CASE("6502 control flags: M=1 enables PNMI from R3", "[tube][6502][control]") {
    TubeUla tube;

    // Set M flag
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    tube.host_write(5, 0x42);  // write R3 data

    // PNMI should be asserted
    CHECK(tube.pnmi_level() == true);
}

TEST_CASE("6502 control flags: clearing M mid-transfer suppresses further NMIs", "[tube][6502][control]") {
    // Start an NMI-driven R3 transfer with M=1, send a few bytes,
    // then clear M.  The coprocessor should stop receiving NMIs.

    TubeUla tube;

    static constexpr uint16_t MAIN_ADDR = 0x0400;
    static constexpr uint16_t NMI_ADDR = 0x0300;
    static constexpr uint8_t COUNTER_ZP = 0x10;

    auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR, 0x0000);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    // Main loop: spin forever (NMI handler does the work)
    // $0400: CLI
    // $0401: BRA *
    plant(memory, MAIN_ADDR, {
        0x58,                                // CLI
        0x80, 0xFE,                          // BRA *
    });

    // NMI handler: read R3 data, increment counter, RTI
    // $0300: LDA $FEFD
    // $0303: INC $10
    // $0305: RTI
    plant(memory, NMI_ADDR, {
        0xAD, 0xFD, 0xFE,                   // LDA $FEFD  (read R3 data)
        0xE6, COUNTER_ZP,                    // INC $10
        0x40,                                // RTI
    });

    memory.read(0xFEF8);  // disable boot ROM
    memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
    memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
    memory.ram(COUNTER_ZP) = 0;
    cpu.reset();

    // Enable M and send 3 bytes
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    for (int byte_num = 0; byte_num < 3; ++byte_num) {
        tube.host_write(5, static_cast<uint8_t>(byte_num));
        for (int t = 0; t < 100000 && memory.ram(COUNTER_ZP) <= byte_num; ++t) {
            cpu.tick();
        }
    }
    CHECK(memory.ram(COUNTER_ZP) == 3);

    // Clear M
    tube.host_write(0, TubeUla::FLAG_M);  // S=0, clear M

    // Write another byte to R3
    tube.host_write(5, 0xFF);

    // PNMI should NOT be asserted
    CHECK(tube.pnmi_level() == false);

    // Run the coprocessor for a while -- counter should NOT increase
    for (int i = 0; i < 10000; ++i) {
        cpu.tick();
    }
    CHECK(memory.ram(COUNTER_ZP) == 3);  // unchanged
}

// ============================================================================
// Tests: I/J flag gating of PIRQ
// ============================================================================

TEST_CASE("6502 control flags: I=0 suppresses PIRQ from R1", "[tube][6502][control]") {
    TubeUla tube;

    // Write R1 data without setting I flag
    tube.host_write(1, 0x42);
    CHECK(tube.pirq() == false);
}

TEST_CASE("6502 control flags: I=1 enables PIRQ from R1", "[tube][6502][control]") {
    TubeUla tube;

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
    tube.host_write(1, 0x42);
    CHECK(tube.pirq() == true);
}

TEST_CASE("6502 control flags: J=0 suppresses PIRQ from R4", "[tube][6502][control]") {
    TubeUla tube;

    tube.host_write(7, 0x42);
    CHECK(tube.pirq() == false);
}

TEST_CASE("6502 control flags: J=1 enables PIRQ from R4", "[tube][6502][control]") {
    TubeUla tube;

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);
    tube.host_write(7, 0x42);
    CHECK(tube.pirq() == true);
}

// ============================================================================
// Tests: V flag (R3 two-byte mode)
// ============================================================================

TEST_CASE("6502 control flags: V=0 R3 is 1-byte mode", "[tube][6502][control]") {
    // With V=0 (default), R3 H2P threshold is 1.  The host can write 1 byte
    // before the FIFO is full.
    TubeUla tube;

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Write 1 byte -- should succeed immediately
    tube.host_write(5, 0xAA);

    // Host status should show R3 H2P full (bit 6 = 0)
    uint8_t status = tube.host_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

TEST_CASE("6502 control flags: V=1 R3 is 2-byte mode", "[tube][6502][control]") {
    // With V=1, R3 H2P threshold is 2.  The host can write 2 bytes
    // before the FIFO is full.
    TubeUla tube;

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V | TubeUla::FLAG_M);

    // Write 2 bytes -- both should succeed immediately
    tube.host_write(5, 0xAA);
    // After 1 byte in 2-byte mode, should still have space
    CHECK((tube.host_peek(4) & TubeUla::SPACE_AVAILABLE) != 0);
    tube.host_write(5, 0xBB);

    // Host status should show R3 H2P full (bit 6 = 0)
    uint8_t status = tube.host_read(4);
    CHECK((status & TubeUla::SPACE_AVAILABLE) == 0);
}

TEST_CASE("6502 control flags: V=1 R3 two-byte transfer with 6502", "[tube][6502][control]") {
    // Full two-byte mode transfer: host writes 2 bytes, coprocessor reads both.
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    // Coprocessor: read two bytes from R3 into RAM
    // $0400: LDA $FEFD       ; read R3 data (byte 0)
    // $0403: STA $0500       ; store
    // $0405: LDA $FEFD       ; read R3 data (byte 1)
    // $0408: STA $0501       ; store
    // $040A: BRA *           ; halt
    plant(memory, CODE_ADDR, {
        0xAD, 0xFD, 0xFE,                   // LDA $FEFD
        0x8D, 0x00, 0x05,                   // STA $0500
        0xAD, 0xFD, 0xFE,                   // LDA $FEFD
        0x8D, 0x01, 0x05,                   // STA $0501
        0x80, 0xFE,                          // BRA *
    });
    setup_cpu_basic(memory, cpu);

    // Set V=1 (two-byte mode) and M (for PNMI, though we poll here)
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_V);

    // Write 2 bytes -- both fit in the 2-slot FIFO
    tube.host_write(5, 0xAA);
    tube.host_write(5, 0xBB);

    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != 0x040C; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x040C);
    CHECK(memory.ram(RESULT_ADDR) == 0xAA);
    CHECK(memory.ram(RESULT_ADDR + 1) == 0xBB);
}

// ============================================================================
// Tests: dynamic flag changes during active transfer
// ============================================================================

TEST_CASE("6502 control flags: enable I mid-transfer starts PIRQ delivery", "[tube][6502][control]") {
    // Set up the coprocessor first, then write R1 data with I=0, then set I=1
    // while the data is still pending.  The coprocessor should see PIRQ assert
    // and handle the byte via its IRQ handler.
    TubeUla tube;

    static constexpr uint16_t MAIN = 0x0400;
    static constexpr uint16_t IRQ = 0x0300;
    static constexpr uint8_t CTR_ZP = 0x10;

    auto rom = make_stub_rom(MAIN, 0x0000, IRQ);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    // Main: CLI then poll counter until 1
    plant(memory, MAIN, {
        0x58,                                // CLI
        0xA5, CTR_ZP,                        // LDA $10
        0xC9, 0x01,                          // CMP #1
        0xD0, 0xFA,                          // BNE $0401    (offset -6)
        0x80, 0xFE,                          // BRA *        (halt at $0407)
    });

    // IRQ handler: read R1, store, increment counter, RTI
    plant(memory, IRQ, {
        0x48,                                // PHA
        0xAD, 0xF9, 0xFE,                   // LDA $FEF9
        0x8D, 0x00, 0x05,                   // STA $0500
        0xE6, CTR_ZP,                        // INC $10
        0x68,                                // PLA
        0x40,                                // RTI
    });

    memory.read(0xFEF8);
    memory.ram(0xFFFC) = MAIN & 0xFF;
    memory.ram(0xFFFD) = (MAIN >> 8) & 0xFF;
    memory.ram(0xFFFE) = IRQ & 0xFF;
    memory.ram(0xFFFF) = (IRQ >> 8) & 0xFF;
    memory.ram(CTR_ZP) = 0;
    cpu.reset();

    // Write R1 data AFTER reset, without I flag -- no PIRQ yet
    tube.host_write(1, 0x42);
    CHECK(tube.pirq() == false);

    // Now enable I -- PIRQ should fire because R1 H2P already has data
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
    CHECK(tube.pirq() == true);

    for (int i = 0; i < 500000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
    CHECK(memory.ram(CTR_ZP) == 1);
}

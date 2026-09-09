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

// PIRQ-driven transfer tests using the real 65C02 CPU.
//
// PIRQ is the coprocessor IRQ line, asserted when:
//   - FLAG_I is set AND R1 H-to-P has data available, OR
//   - FLAG_J is set AND R4 H-to-P has data available.
//
// These tests use IRQ handlers to read incoming data, rather than polling
// status registers.  This is the typical pattern for Tube Host Code
// transfers where the coprocessor runs an IRQ-driven receive loop.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorCpu.hpp>
#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

static std::array<uint8_t, 4096> make_stub_rom(uint16_t reset_addr, uint16_t irq_addr) {
    std::array<uint8_t, 4096> rom{};
    rom[0xFFC] = reset_addr & 0xFF;        // Reset vector at $FFFC
    rom[0xFFD] = (reset_addr >> 8) & 0xFF;
    rom[0xFFE] = irq_addr & 0xFF;          // IRQ/BRK vector at $FFFE
    rom[0xFFF] = (irq_addr >> 8) & 0xFF;
    return rom;
}

static void plant(CoprocessorMemoryMap& mem, uint16_t addr, std::initializer_list<uint8_t> code) {
    for (auto byte : code) {
        mem.ram(addr++) = byte;
    }
}

static constexpr uint16_t MAIN_ADDR = 0x0400;
static constexpr uint16_t IRQ_ADDR = 0x0300;
static constexpr uint16_t RESULT_ADDR = 0x0500;
static constexpr uint8_t COUNTER_ZP = 0x10;

// ============================================================================
// 6502 R1 IRQ-driven receive program
// ============================================================================
//
// Main code at $0400:
//   $0400: CLI              ; enable IRQ
//   $0401: LDA $10          ; check byte counter
//   $0403: CMP #NUM_BYTES   ; received all bytes?
//   $0405: BNE $0401        ; loop until done  (offset -6 = $FA)
//   $0407: BRA *            ; halt
//
// IRQ handler at $0300:
//   $0300: PHA              ; save A
//   $0301: PHX              ; save X
//   $0302: LDX $10          ; load byte counter
//   $0304: LDA $FEF9        ; read R1 data (clears PIRQ for R1)
//   $0307: STA $0500,X      ; store in results buffer
//   $030A: INC $10          ; increment counter
//   $030C: PLX              ; restore X
//   $030D: PLA              ; restore A
//   $030E: RTI              ; return from interrupt

static void plant_r1_irq_receiver(CoprocessorMemoryMap& mem, uint8_t num_bytes) {
    // Main loop
    plant(mem, MAIN_ADDR, {
        0x58,                                // CLI
        0xA5, COUNTER_ZP,                    // LDA $10
        0xC9, num_bytes,                     // CMP #num_bytes
        0xD0, 0xFA,                          // BNE $0401   (offset -6)
        0x80, 0xFE,                          // BRA *        (halt)
    });

    // IRQ handler
    plant(mem, IRQ_ADDR, {
        0x48,                                // PHA
        0xDA,                                // PHX        (65C02)
        0xA6, COUNTER_ZP,                    // LDX $10
        0xAD, 0xF9, 0xFE,                   // LDA $FEF9  (read R1 data)
        0x9D, 0x00, 0x05,                    // STA $0500,X
        0xE6, COUNTER_ZP,                    // INC $10
        0xFA,                                // PLX        (65C02)
        0x68,                                // PLA
        0x40,                                // RTI
    });
}

// ============================================================================
// 6502 R4 IRQ-driven receive program
// ============================================================================
//
// Same structure as R1 but reads from $FEFF (R4 data).

static void plant_r4_irq_receiver(CoprocessorMemoryMap& mem, uint8_t num_bytes) {
    // Main loop
    plant(mem, MAIN_ADDR, {
        0x58,                                // CLI
        0xA5, COUNTER_ZP,                    // LDA $10
        0xC9, num_bytes,                     // CMP #num_bytes
        0xD0, 0xFA,                          // BNE $0401   (offset -6)
        0x80, 0xFE,                          // BRA *        (halt)
    });

    // IRQ handler
    plant(mem, IRQ_ADDR, {
        0x48,                                // PHA
        0xDA,                                // PHX        (65C02)
        0xA6, COUNTER_ZP,                    // LDX $10
        0xAD, 0xFF, 0xFE,                   // LDA $FEFF  (read R4 data)
        0x9D, 0x00, 0x05,                    // STA $0500,X
        0xE6, COUNTER_ZP,                    // INC $10
        0xFA,                                // PLX        (65C02)
        0x68,                                // PLA
        0x40,                                // RTI
    });
}

static void setup_cpu(CoprocessorMemoryMap& mem, CoprocessorCpu& cpu) {
    mem.read(0xFEF8);  // disable boot ROM
    mem.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    mem.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    mem.ram(0xFFFE) = IRQ_ADDR & 0xFF;
    mem.ram(0xFFFF) = (IRQ_ADDR >> 8) & 0xFF;
    mem.ram(COUNTER_ZP) = 0;
    cpu.reset();
}

// ============================================================================
// R1 PIRQ tests (FLAG_I)
// ============================================================================

TEST_CASE("6502 PIRQ R1: single byte", "[tube][6502][pirq]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, IRQ_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r1_irq_receiver(memory, 1);
    setup_cpu(memory, cpu);

    // Enable PIRQ from R1 (FLAG_I)
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);

    // Write one byte -- PIRQ should fire
    tube.host_write(1, 0x42);

    for (int i = 0; i < 500000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
    CHECK(memory.ram(COUNTER_ZP) == 1);
}

TEST_CASE("6502 PIRQ R1: 200 bytes interleaved", "[tube][6502][pirq]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, IRQ_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r1_irq_receiver(memory, NUM_BYTES);
    setup_cpu(memory, cpu);

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);

    int host_written = 0;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        if (host_written < NUM_BYTES && (tube.host_peek(0) & TubeUla::SPACE_AVAILABLE)) {
            tube.host_write(1, static_cast<uint8_t>(host_written & 0xFF));
            ++host_written;
        }
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(memory.ram(RESULT_ADDR + i) == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 PIRQ R1: 200 bytes, repeated 50 times", "[tube][6502][pirq]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(MAIN_ADDR, IRQ_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r1_irq_receiver(memory, NUM_BYTES);
        setup_cpu(memory, cpu);

        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_I);
        uint8_t base = static_cast<uint8_t>(iter * 11);

        int host_written = 0;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
            if (host_written < NUM_BYTES && (tube.host_peek(0) & TubeUla::SPACE_AVAILABLE)) {
                tube.host_write(1, static_cast<uint8_t>((base + host_written) & 0xFF));
                ++host_written;
            }
            cpu.tick();
        }

        REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);

        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF);
            if (memory.ram(RESULT_ADDR + i) != expected) {
                FAIL("Iteration " << iter << ", byte " << i
                     << ": expected $" << std::hex << static_cast<int>(expected)
                     << " got $" << std::hex << static_cast<int>(memory.ram(RESULT_ADDR + i)));
            }
        }
    }
}

// ============================================================================
// R4 PIRQ tests (FLAG_J)
// ============================================================================

TEST_CASE("6502 PIRQ R4: single byte", "[tube][6502][pirq]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, IRQ_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r4_irq_receiver(memory, 1);
    setup_cpu(memory, cpu);

    // Enable PIRQ from R4 (FLAG_J)
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);

    tube.host_write(7, 0x42);

    for (int i = 0; i < 500000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
    CHECK(memory.ram(COUNTER_ZP) == 1);
}

TEST_CASE("6502 PIRQ R4: 200 bytes interleaved", "[tube][6502][pirq]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, IRQ_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r4_irq_receiver(memory, NUM_BYTES);
    setup_cpu(memory, cpu);

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);

    int host_written = 0;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        if (host_written < NUM_BYTES && (tube.host_peek(6) & TubeUla::SPACE_AVAILABLE)) {
            tube.host_write(7, static_cast<uint8_t>(host_written & 0xFF));
            ++host_written;
        }
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(memory.ram(RESULT_ADDR + i) == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 PIRQ R4: 200 bytes, repeated 50 times", "[tube][6502][pirq]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(MAIN_ADDR, IRQ_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r4_irq_receiver(memory, NUM_BYTES);
        setup_cpu(memory, cpu);

        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_J);
        uint8_t base = static_cast<uint8_t>(iter * 11);

        int host_written = 0;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
            if (host_written < NUM_BYTES && (tube.host_peek(6) & TubeUla::SPACE_AVAILABLE)) {
                tube.host_write(7, static_cast<uint8_t>((base + host_written) & 0xFF));
                ++host_written;
            }
            cpu.tick();
        }

        REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);

        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF);
            if (memory.ram(RESULT_ADDR + i) != expected) {
                FAIL("Iteration " << iter << ", byte " << i
                     << ": expected $" << std::hex << static_cast<int>(expected)
                     << " got $" << std::hex << static_cast<int>(memory.ram(RESULT_ADDR + i)));
            }
        }
    }
}

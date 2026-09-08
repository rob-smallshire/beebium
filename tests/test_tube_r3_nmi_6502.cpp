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

// R3 NMI transfer tests using the real 65C02 CPU.
//
// The host writes bytes through R3 (with M flag set to enable PNMI).
// The coprocessor 6502 has a minimal NMI handler that reads R3 data and
// stores it into a results buffer. This tests the full NMI-driven
// transfer path: host_write -> PNMI -> NMI handler -> coprocessor_read
// -> RAM, with concurrent execution.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorCpu.hpp>
#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

static std::array<uint8_t, 2048> make_stub_rom(uint16_t reset_addr, uint16_t nmi_addr) {
    std::array<uint8_t, 2048> rom{};
    rom[0x07FC] = reset_addr & 0xFF;        // Reset vector at $FFFC
    rom[0x07FD] = (reset_addr >> 8) & 0xFF;
    rom[0x07FA] = nmi_addr & 0xFF;          // NMI vector at $FFFA
    rom[0x07FB] = (nmi_addr >> 8) & 0xFF;
    return rom;
}

static void plant(CoprocessorMemoryMap& mem, uint16_t addr, std::initializer_list<uint8_t> code) {
    for (auto byte : code) {
        mem.ram(addr++) = byte;
    }
}

// ============================================================================
// 6502 R3 NMI receive program
// ============================================================================
//
// Main code at $0400:
//   $0400: CLI              ; enable interrupts (allow NMI to fire)
//   $0401: LDA $10          ; check byte counter (in zero page)
//   $0403: CMP #NUM_BYTES   ; received all bytes?
//   $0405: BNE $0401        ; loop until done
//   $0407: BRA *            ; halt
//
// NMI handler at $0300:
//   $0300: PHA              ; save A
//   $0301: PHX              ; save X
//   $0302: LDX $10          ; load byte counter
//   $0304: LDA $FEFD        ; read R3 data
//   $0307: STA $0500,X      ; store in results buffer
//   $030A: INC $10          ; increment counter
//   $030C: PLX              ; restore X
//   $030D: PLA              ; restore A
//   $030E: RTI              ; return from interrupt
//
// Zero page $10 = byte counter (starts at 0)

static constexpr uint16_t MAIN_ADDR = 0x0400;
static constexpr uint16_t NMI_ADDR = 0x0300;
static constexpr uint16_t RESULT_ADDR = 0x0500;
static constexpr uint8_t COUNTER_ZP = 0x10;

static void plant_r3_receiver(CoprocessorMemoryMap& mem, uint8_t num_bytes) {
    // Main loop: wait until counter reaches num_bytes
    plant(mem, MAIN_ADDR, {
        0x58,                                // CLI
        0xA5, COUNTER_ZP,                    // LDA $10
        0xC9, num_bytes,                     // CMP #num_bytes
        0xD0, 0xFA,                          // BNE $0401   (offset -6)
        0x80, 0xFE,                          // BRA *        (halt)
    });

    // NMI handler
    plant(mem, NMI_ADDR, {
        0x48,                                // PHA
        0xDA,                                // PHX        (65C02)
        0xA6, COUNTER_ZP,                    // LDX $10
        0xAD, 0xFD, 0xFE,                   // LDA $FEFD  (R3 data)
        0x9D, 0x00, 0x05,                    // STA $0500,X
        0xE6, COUNTER_ZP,                    // INC $10
        0xFA,                                // PLX        (65C02)
        0x68,                                // PLA
        0x40,                                // RTI
    });
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("6502 R3 NMI transfer: single byte", "[tube][6502][r3]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r3_receiver(memory, 1);
    memory.read(0xFEF8);  // disable boot ROM
    memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
    memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
    memory.ram(COUNTER_ZP) = 0;
    cpu.reset();

    // Host: enable M flag and write one R3 byte synchronously
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    tube.host_write(5, 0x42);

    // Verify R3 state before ticking
    INFO("pnmi_level: " << tube.pnmi_level());

    // R3 H2P should have data available (host status bit 6 should be 0 = full)
    REQUIRE((tube.host_peek(4) & TubeUla::SPACE_AVAILABLE) == 0);
    REQUIRE(tube.pnmi_level() == true);

    for (int i = 0; i < 500000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);  // reached halt
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
    CHECK(memory.ram(COUNTER_ZP) == 1);
}

TEST_CASE("6502 R3 NMI transfer: 2 bytes synchronous", "[tube][6502][r3]") {
    // Two bytes, no threads. Write byte 1, tick until NMI handler
    // processes it, write byte 2, tick until done.
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r3_receiver(memory, 2);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
    memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
    memory.ram(COUNTER_ZP) = 0;
    cpu.reset();

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Write byte 1
    tube.host_write(5, 0xAA);
    REQUIRE(tube.pnmi_level() == true);

    // Tick until NMI handler reads byte 1 (counter becomes 1)
    for (int i = 0; i < 100000 && memory.ram(COUNTER_ZP) < 1; ++i) {
        cpu.tick();
    }
    CHECK(memory.ram(COUNTER_ZP) == 1);
    CHECK(memory.ram(RESULT_ADDR) == 0xAA);

    // Write byte 2
    tube.host_write(5, 0xBB);

    // Tick until NMI handler reads byte 2 (counter becomes 2)
    for (int i = 0; i < 100000 && memory.ram(COUNTER_ZP) < 2; ++i) {
        cpu.tick();
    }
    CHECK(memory.ram(COUNTER_ZP) == 2);
    CHECK(memory.ram(RESULT_ADDR + 1) == 0xBB);
}

TEST_CASE("6502 R3 NMI transfer: 200 bytes synchronous", "[tube][6502][r3]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r3_receiver(memory, NUM_BYTES);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
    memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
    memory.ram(COUNTER_ZP) = 0;
    cpu.reset();

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    // Write each byte then tick until consumed
    for (int byte_num = 0; byte_num < NUM_BYTES; ++byte_num) {
        tube.host_write(5, static_cast<uint8_t>(byte_num & 0xFF));
        for (int t = 0; t < 100000 && memory.ram(COUNTER_ZP) <= byte_num; ++t) {
            cpu.tick();
        }
        if (memory.ram(COUNTER_ZP) <= byte_num) {
            FAIL("Stuck at byte " << byte_num << ": counter=" << static_cast<int>(memory.ram(COUNTER_ZP)));
        }
    }

    // Wait for main loop to halt
    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(memory.ram(RESULT_ADDR + i) == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 R3 NMI transfer: 200 bytes interleaved", "[tube][6502][r3]") {
    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r3_receiver(memory, NUM_BYTES);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
    memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
    memory.ram(COUNTER_ZP) = 0;
    cpu.reset();

    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

    int host_written = 0;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        if (host_written < NUM_BYTES && (tube.host_peek(4) & TubeUla::SPACE_AVAILABLE)) {
            tube.host_write(5, static_cast<uint8_t>(host_written & 0xFF));
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

TEST_CASE("6502 R3 NMI transfer: in_nmi_handler is set during handler", "[tube][6502][r3][diagnostic]") {
    // Verify that in_nmi_handler_ transitions to true when the NMI fires
    // and back to false after RTI.  This is the core invariant that the
    // pre-tfn detection fix ensures.

    TubeUla tube;

    auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r3_receiver(memory, 1);
    memory.read(0xFEF8);
    memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
    memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
    memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
    memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
    memory.ram(COUNTER_ZP) = 0;
    cpu.reset();

    // Write one byte with M flag to trigger PNMI
    tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);
    tube.host_write(5, 0x42);
    REQUIRE(tube.pnmi_level() == true);

    bool saw_nmi_handler_true = false;
    bool saw_nmi_handler_false_after_true = false;

    for (int i = 0; i < 500000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
        cpu.tick();
        if (cpu.in_nmi_handler()) {
            saw_nmi_handler_true = true;
        } else if (saw_nmi_handler_true) {
            saw_nmi_handler_false_after_true = true;
        }
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0407);
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
    CHECK(saw_nmi_handler_true);
    CHECK(saw_nmi_handler_false_after_true);
}

TEST_CASE("6502 R3 NMI transfer: 200 bytes, repeated 50 times", "[tube][6502][r3]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(MAIN_ADDR, NMI_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r3_receiver(memory, NUM_BYTES);
        memory.read(0xFEF8);
        memory.ram(0xFFFC) = MAIN_ADDR & 0xFF;
        memory.ram(0xFFFD) = (MAIN_ADDR >> 8) & 0xFF;
        memory.ram(0xFFFA) = NMI_ADDR & 0xFF;
        memory.ram(0xFFFB) = (NMI_ADDR >> 8) & 0xFF;
        memory.ram(COUNTER_ZP) = 0;
        cpu.reset();

        uint8_t base = static_cast<uint8_t>(iter * 11);
        tube.host_write(0, TubeUla::FLAG_S | TubeUla::FLAG_M);

        int host_written = 0;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0407; ++i) {
            if (host_written < NUM_BYTES && (tube.host_peek(4) & TubeUla::SPACE_AVAILABLE)) {
                tube.host_write(5, static_cast<uint8_t>((base + host_written) & 0xFF));
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

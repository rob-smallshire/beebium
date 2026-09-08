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

// R3 P-to-H transfer tests using the real 65C02 CPU.
//
// The coprocessor writes bytes to R3 P-to-H via $FEFD.  The host reads them
// from R3 data (offset 5).  The coprocessor polls R3 status ($FEFC) bit 6
// (space available) before each write.
//
// R3 is a 2-slot circular buffer (or 1-slot when V=0).  These tests use
// the default V=0 (1-byte mode) for simplicity.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorCpu.hpp>
#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

static std::array<uint8_t, 2048> make_stub_rom(uint16_t reset_addr) {
    std::array<uint8_t, 2048> rom{};
    rom[0x07FC] = reset_addr & 0xFF;
    rom[0x07FD] = (reset_addr >> 8) & 0xFF;
    return rom;
}

static void plant(CoprocessorMemoryMap& mem, uint16_t addr, std::initializer_list<uint8_t> code) {
    for (auto byte : code) {
        mem.ram(addr++) = byte;
    }
}

static constexpr uint16_t CODE_ADDR = 0x0400;
static constexpr uint16_t SOURCE_ADDR = 0x0500;

// ============================================================================
// 6502 R3 polled write program (Coprocessor-to-Host)
// ============================================================================
//
// $0400: LDX #$00
// $0402: BIT $FEFC        ; poll R3 status
// $0405: BVC $0402        ; loop until bit 6 set (space available)
// $0407: LDA $0500,X      ; load byte from source buffer
// $040A: STA $FEFD        ; write R3 data
// $040D: INX
// $040E: CPX #NUM_BYTES
// $0410: BNE $0402        ; loop
// $0412: BRA *            ; halt

static void plant_r3_writer(CoprocessorMemoryMap& mem, uint8_t num_bytes) {
    plant(mem, CODE_ADDR, {
        0xA2, 0x00,                         // LDX #$00
        0x2C, 0xFC, 0xFE,                   // BIT $FEFC     (poll R3 status)
        0x50, 0xFB,                          // BVC $0402
        0xBD, 0x00, 0x05,                   // LDA $0500,X
        0x8D, 0xFD, 0xFE,                   // STA $FEFD     (write R3 data)
        0xE8,                                // INX
        0xE0, num_bytes,                     // CPX #num_bytes
        0xD0, 0xF0,                          // BNE $0402
        0x80, 0xFE,                          // BRA *         (halt)
    });
}

static void setup_cpu(CoprocessorMemoryMap& mem, CoprocessorCpu& cpu, TubeUla& tube) {
    mem.read(0xFEF8);  // disable boot ROM
    mem.ram(0xFFFC) = CODE_ADDR & 0xFF;
    mem.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
    cpu.reset();
    // TubeUla::reset() seeds R3 P-to-H with a dummy byte (count=1)
    // to prevent spurious PNMI.  Drain it so the coprocessor has space to write.
    tube.host_read(5);
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("6502 R3 P2H: single byte", "[tube][6502][r3]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r3_writer(memory, 1);
    memory.ram(SOURCE_ADDR) = 0x42;
    setup_cpu(memory, cpu, tube);

    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);
    CHECK(tube.host_read(5) == 0x42);
}

TEST_CASE("6502 R3 P2H: 200 bytes interleaved", "[tube][6502][r3]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r3_writer(memory, NUM_BYTES);
    for (int i = 0; i < NUM_BYTES; ++i) {
        memory.ram(SOURCE_ADDR + i) = static_cast<uint8_t>(i & 0xFF);
    }
    setup_cpu(memory, cpu, tube);

    std::array<uint8_t, NUM_BYTES> received{};

    int host_read_idx = 0;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
        if (host_read_idx < NUM_BYTES && (tube.host_read(4) & TubeUla::DATA_AVAILABLE)) {
            received[host_read_idx] = tube.host_read(5);
            ++host_read_idx;
        }
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 R3 P2H: 200 bytes, repeated 50 times", "[tube][6502][r3]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(CODE_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r3_writer(memory, NUM_BYTES);
        uint8_t base = static_cast<uint8_t>(iter * 11);
        for (int i = 0; i < NUM_BYTES; ++i) {
            memory.ram(SOURCE_ADDR + i) = static_cast<uint8_t>((base + i) & 0xFF);
        }
        setup_cpu(memory, cpu, tube);

        std::array<uint8_t, NUM_BYTES> received{};

        int host_read_idx = 0;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
            cpu.tick();
            if (host_read_idx < NUM_BYTES && (tube.host_read(4) & TubeUla::DATA_AVAILABLE)) {
                received[host_read_idx] = tube.host_read(5);
                ++host_read_idx;
            }
        }

        REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);

        for (int i = 0; i < NUM_BYTES; ++i) {
            uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF);
            if (received[i] != expected) {
                FAIL("Iteration " << iter << ", byte " << i
                     << ": expected $" << std::hex << static_cast<int>(expected)
                     << " got $" << std::hex << static_cast<int>(received[i]));
            }
        }
    }
}

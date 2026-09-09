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

// R2 transfer tests using the real 65C02 CPU.
//
// R2 is a single-byte latch in each direction with no bus stretching and no
// interrupt support.  Unlike R1/R3/R4, host_write to R2 does NOT spin until
// the coprocessor consumes the previous byte -- it unconditionally overwrites
// the latch.  The host must therefore poll R2 status (or spin on the shared
// ready flag) to avoid data loss.
//
// Host-to-Coprocessor: host waits for r2_h2p.ready == 0 (coprocessor consumed
// previous byte), then writes R2 data (offset 3).  Coprocessor polls R2 status
// ($FEFA) bit 7 then reads R2 data ($FEFB).
//
// Coprocessor-to-Host: coprocessor polls R2 status ($FEFA) bit 6 then writes R2
// data ($FEFB).  Host waits for r2_p2h.ready != 0 (coprocessor has written),
// then reads R2 data (offset 3).
//
// Host-side operations are interleaved with coprocessor CPU ticks in a single
// thread, checking Tube status flags each iteration to decide when to act.

#include <catch2/catch_test_macros.hpp>

#include <beebium/tube/CoprocessorCpu.hpp>
#include <beebium/tube/CoprocessorMemoryMap.hpp>
#include <beebium/tube/TubeUla.hpp>

#include <array>
#include <cstdint>

using namespace beebium;

static std::array<uint8_t, 4096> make_stub_rom(uint16_t reset_addr) {
    std::array<uint8_t, 4096> rom{};
    rom[0xFFC] = reset_addr & 0xFF;
    rom[0xFFD] = (reset_addr >> 8) & 0xFF;
    return rom;
}

static void plant(CoprocessorMemoryMap& mem, uint16_t addr, std::initializer_list<uint8_t> code) {
    for (auto byte : code) {
        mem.ram(addr++) = byte;
    }
}

static constexpr uint16_t CODE_ADDR = 0x0400;
static constexpr uint16_t RESULT_ADDR = 0x0500;

// ============================================================================
// 6502 R2 polled read program (Host-to-Coprocessor)
// ============================================================================
//
// $0400: LDX #$00
// $0402: BIT $FEFA        ; poll R2 status
// $0405: BPL $0402        ; loop until bit 7 set (data available)
// $0407: LDA $FEFB        ; read R2 data
// $040A: STA $0500,X      ; store in results buffer
// $040D: INX
// $040E: CPX #NUM_BYTES
// $0410: BNE $0402        ; loop
// $0412: BRA *            ; halt

static void plant_r2_reader(CoprocessorMemoryMap& mem, uint8_t num_bytes) {
    plant(mem, CODE_ADDR, {
        0xA2, 0x00,                         // LDX #$00
        0x2C, 0xFA, 0xFE,                   // BIT $FEFA     (poll R2 status)
        0x10, 0xFB,                          // BPL $0402
        0xAD, 0xFB, 0xFE,                   // LDA $FEFB     (read R2 data)
        0x9D, 0x00, 0x05,                   // STA $0500,X
        0xE8,                                // INX
        0xE0, num_bytes,                     // CPX #num_bytes
        0xD0, 0xF0,                          // BNE $0402
        0x80, 0xFE,                          // BRA *         (halt)
    });
}

// ============================================================================
// 6502 R2 polled write program (Coprocessor-to-Host)
// ============================================================================
//
// $0400: LDX #$00
// $0402: BIT $FEFA        ; poll R2 status
// $0405: BVC $0402        ; loop until bit 6 set (space available)
// $0407: LDA $0500,X      ; load byte from source buffer
// $040A: STA $FEFB        ; write R2 data
// $040D: INX
// $040E: CPX #NUM_BYTES
// $0410: BNE $0402        ; loop
// $0412: BRA *            ; halt

static void plant_r2_writer(CoprocessorMemoryMap& mem, uint8_t num_bytes) {
    plant(mem, CODE_ADDR, {
        0xA2, 0x00,                         // LDX #$00
        0x2C, 0xFA, 0xFE,                   // BIT $FEFA     (poll R2 status)
        0x50, 0xFB,                          // BVC $0402
        0xBD, 0x00, 0x05,                   // LDA $0500,X
        0x8D, 0xFB, 0xFE,                   // STA $FEFB     (write R2 data)
        0xE8,                                // INX
        0xE0, num_bytes,                     // CPX #num_bytes
        0xD0, 0xF0,                          // BNE $0402
        0x80, 0xFE,                          // BRA *         (halt)
    });
}

// ============================================================================
// 6502 R2 command/response program
// ============================================================================
//
// Models the real R2 protocol: read a command byte, process it, write back
// a response byte.  The transform is EOR #$FF (bitwise complement).
//
// $0400: LDX #$00
// $0402: BIT $FEFA        ; poll R2 status
// $0405: BPL $0402        ; loop until bit 7 (command available)
// $0407: LDA $FEFB        ; read command byte
// $040A: EOR #$FF         ; transform: complement
// $040C: BIT $FEFA        ; poll R2 status
// $040F: BVC $040C        ; loop until bit 6 (space to write response)
// $0411: STA $FEFB        ; write response byte
// $0414: INX
// $0415: CPX #NUM_BYTES
// $0417: BNE $0402        ; loop
// $0419: BRA *            ; halt

static constexpr uint16_t CMD_RSP_HALT = 0x0419;

static void plant_r2_cmd_rsp(CoprocessorMemoryMap& mem, uint8_t num_commands) {
    plant(mem, CODE_ADDR, {
        0xA2, 0x00,                         // LDX #$00
        0x2C, 0xFA, 0xFE,                   // BIT $FEFA     (poll R2 status)
        0x10, 0xFB,                          // BPL $0402     (wait for command)
        0xAD, 0xFB, 0xFE,                   // LDA $FEFB     (read command)
        0x49, 0xFF,                          // EOR #$FF      (complement)
        0x2C, 0xFA, 0xFE,                   // BIT $FEFA     (poll R2 status)
        0x50, 0xFB,                          // BVC $040C     (wait for space)
        0x8D, 0xFB, 0xFE,                   // STA $FEFB     (write response)
        0xE8,                                // INX
        0xE0, num_commands,                  // CPX #num_commands
        0xD0, 0xE9,                          // BNE $0402     (loop)
        0x80, 0xFE,                          // BRA *         (halt)
    });
}

static void setup_cpu(CoprocessorMemoryMap& mem, CoprocessorCpu& cpu) {
    mem.read(0xFEF8);  // disable boot ROM
    mem.ram(0xFFFC) = CODE_ADDR & 0xFF;
    mem.ram(0xFFFD) = (CODE_ADDR >> 8) & 0xFF;
    cpu.reset();
}

// ============================================================================
// Host-to-Coprocessor tests
// ============================================================================

TEST_CASE("6502 R2 H2P: single byte", "[tube][6502][r2]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r2_reader(memory, 1);
    setup_cpu(memory, cpu);

    tube.host_write(3, 0x42);

    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);
    CHECK(memory.ram(RESULT_ADDR) == 0x42);
}

TEST_CASE("6502 R2 H2P: 200 bytes interleaved", "[tube][6502][r2]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r2_reader(memory, NUM_BYTES);
    setup_cpu(memory, cpu);

    // R2 has no bus stretching -- the host must wait for the coprocessor to
    // consume each byte before writing the next, otherwise it overwrites
    // the latch and data is lost.
    int host_written = 0;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        if (host_written < NUM_BYTES && (tube.host_peek(2) & TubeUla::SPACE_AVAILABLE)) {
            tube.host_write(3, static_cast<uint8_t>(host_written & 0xFF));
            ++host_written;
        }
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(memory.ram(RESULT_ADDR + i) == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 R2 H2P: 200 bytes, repeated 50 times", "[tube][6502][r2]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(CODE_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r2_reader(memory, NUM_BYTES);
        setup_cpu(memory, cpu);

        uint8_t base = static_cast<uint8_t>(iter * 11);

        int host_written = 0;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
            if (host_written < NUM_BYTES && (tube.host_peek(2) & TubeUla::SPACE_AVAILABLE)) {
                tube.host_write(3, static_cast<uint8_t>((base + host_written) & 0xFF));
                ++host_written;
            }
            cpu.tick();
        }

        REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);

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
// Coprocessor-to-Host tests
// ============================================================================

TEST_CASE("6502 R2 P2H: single byte", "[tube][6502][r2]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r2_writer(memory, 1);
    memory.ram(RESULT_ADDR) = 0x42;  // source data
    setup_cpu(memory, cpu);

    // Run coprocessor until it halts (has written the byte)
    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);
    CHECK(tube.host_read(3) == 0x42);
}

TEST_CASE("6502 R2 P2H: 200 bytes interleaved", "[tube][6502][r2]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_BYTES = 200;
    plant_r2_writer(memory, NUM_BYTES);

    // Fill source buffer
    for (int i = 0; i < NUM_BYTES; ++i) {
        memory.ram(RESULT_ADDR + i) = static_cast<uint8_t>(i & 0xFF);
    }
    setup_cpu(memory, cpu);

    std::array<uint8_t, NUM_BYTES> received{};

    // R2 P-to-H has no bus stretching -- the host must wait for data
    // to become available before reading, otherwise it reads stale data.
    int host_read_idx = 0;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
        cpu.tick();
        if (host_read_idx < NUM_BYTES && (tube.host_peek(2) & TubeUla::DATA_AVAILABLE)) {
            received[host_read_idx] = tube.host_read(3);
            ++host_read_idx;
        }
    }

    REQUIRE(cpu.cpu().opcode_pc.w == 0x0412);

    for (int i = 0; i < NUM_BYTES; ++i) {
        INFO("byte " << i);
        CHECK(received[i] == static_cast<uint8_t>(i & 0xFF));
    }
}

TEST_CASE("6502 R2 P2H: 200 bytes, repeated 50 times", "[tube][6502][r2]") {
    constexpr uint8_t NUM_BYTES = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(CODE_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r2_writer(memory, NUM_BYTES);
        uint8_t base = static_cast<uint8_t>(iter * 11);
        for (int i = 0; i < NUM_BYTES; ++i) {
            memory.ram(RESULT_ADDR + i) = static_cast<uint8_t>((base + i) & 0xFF);
        }
        setup_cpu(memory, cpu);

        std::array<uint8_t, NUM_BYTES> received{};

        int host_read_idx = 0;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != 0x0412; ++i) {
            cpu.tick();
            if (host_read_idx < NUM_BYTES && (tube.host_peek(2) & TubeUla::DATA_AVAILABLE)) {
                received[host_read_idx] = tube.host_read(3);
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

// ============================================================================
// Command/Response tests (realistic R2 protocol)
// ============================================================================

TEST_CASE("6502 R2 command/response: single round-trip", "[tube][6502][r2]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    plant_r2_cmd_rsp(memory, 1);
    setup_cpu(memory, cpu);

    // Host sends command
    tube.host_write(3, 0x42);

    // Run coprocessor until it halts
    for (int i = 0; i < 100000 && cpu.cpu().opcode_pc.w != CMD_RSP_HALT; ++i) {
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == CMD_RSP_HALT);
    CHECK(tube.host_read(3) == static_cast<uint8_t>(0x42 ^ 0xFF));
}

TEST_CASE("6502 R2 command/response: 200 round-trips interleaved", "[tube][6502][r2]") {
    TubeUla tube;

    auto rom = make_stub_rom(CODE_ADDR);
    CoprocessorMemoryMap memory(tube, rom);
    CoprocessorCpu cpu(memory, tube);

    constexpr uint8_t NUM_CMDS = 200;
    plant_r2_cmd_rsp(memory, NUM_CMDS);
    setup_cpu(memory, cpu);

    std::array<uint8_t, NUM_CMDS> responses{};

    // Host state machine: send command, poll for response, read it.
    // The request/response protocol provides natural flow control --
    // the host waits for the response before sending the next command,
    // so the latch is always empty when we write.
    int cmd_idx = 0;
    bool waiting_for_response = false;
    for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != CMD_RSP_HALT; ++i) {
        if (cmd_idx < NUM_CMDS) {
            if (!waiting_for_response) {
                tube.host_write(3, static_cast<uint8_t>(cmd_idx & 0xFF));
                waiting_for_response = true;
            } else if (tube.host_read(2) & TubeUla::DATA_AVAILABLE) {
                responses[cmd_idx] = tube.host_read(3);
                ++cmd_idx;
                waiting_for_response = false;
            }
        }
        cpu.tick();
    }

    REQUIRE(cpu.cpu().opcode_pc.w == CMD_RSP_HALT);

    for (int i = 0; i < NUM_CMDS; ++i) {
        uint8_t expected = static_cast<uint8_t>(i & 0xFF) ^ 0xFF;
        INFO("command " << i);
        CHECK(responses[i] == expected);
    }
}

TEST_CASE("6502 R2 command/response: 200 round-trips, repeated 50 times", "[tube][6502][r2]") {
    constexpr uint8_t NUM_CMDS = 200;
    constexpr int ITERATIONS = 50;

    for (int iter = 0; iter < ITERATIONS; ++iter) {
        TubeUla tube;

        auto rom = make_stub_rom(CODE_ADDR);
        CoprocessorMemoryMap memory(tube, rom);
        CoprocessorCpu cpu(memory, tube);

        plant_r2_cmd_rsp(memory, NUM_CMDS);
        setup_cpu(memory, cpu);

        uint8_t base = static_cast<uint8_t>(iter * 11);
        std::array<uint8_t, NUM_CMDS> responses{};

        int cmd_idx = 0;
        bool waiting_for_response = false;
        for (int i = 0; i < 10000000 && cpu.cpu().opcode_pc.w != CMD_RSP_HALT; ++i) {
            if (cmd_idx < NUM_CMDS) {
                if (!waiting_for_response) {
                    tube.host_write(3, static_cast<uint8_t>((base + cmd_idx) & 0xFF));
                    waiting_for_response = true;
                } else if (tube.host_read(2) & TubeUla::DATA_AVAILABLE) {
                    responses[cmd_idx] = tube.host_read(3);
                    ++cmd_idx;
                    waiting_for_response = false;
                }
            }
            cpu.tick();
        }

        REQUIRE(cpu.cpu().opcode_pc.w == CMD_RSP_HALT);

        for (int i = 0; i < NUM_CMDS; ++i) {
            uint8_t expected = static_cast<uint8_t>((base + i) & 0xFF) ^ 0xFF;
            if (responses[i] != expected) {
                FAIL("Iteration " << iter << ", command " << i
                     << ": expected $" << std::hex << static_cast<int>(expected)
                     << " got $" << std::hex << static_cast<int>(responses[i]));
            }
        }
    }
}

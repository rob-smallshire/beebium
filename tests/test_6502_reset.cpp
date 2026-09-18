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

// Reset-sequence flag behaviour of the 6502 core (issue #78).
//
// These tests specify what the RESET sequence must do to the processor
// status register, independently of the Beebium machine. They are written
// against documented hardware behaviour so each assertion traces to a
// primary source rather than to an emulator's current output:
//
//   * MOS Technology MCS6500 Microcomputer Family Programming Manual
//     (January 1976), s3.2: "The interrupt disable, I, is set by the
//     microprocessor during reset and interrupt commands." s9.1-9.3 (start
//     cycle table): three stack-address cycles with no write, the vector is
//     read from FFFC then FFFD, and the first opcode is fetched on cycle 8;
//     "the only automatic operations of the microprocessor during reset are
//     to turn on the interrupt disable bit and to force the program counter
//     to the vector location specified in locations FFFC and FFFD."
//   * MCS6500 Hardware Manual s1.4.1.2.11 (RES) and s1.4.1.2.9 (IRQ is
//     recognised only when I is clear).
//   * WDC W65C02S datasheet s3.11: on reset the hardware initialises I = 1
//     and D = 0; on NMOS the decimal flag is indeterminate (unchanged by the
//     reset sequence), per the W65C02S-vs-NMOS comparison table.
//
// Consequences encoded here: RESET sets I on every 6502 variant (so a
// pending IRQ stays masked until the reset handler executes CLI); CMOS parts
// (65C02 / Rockwell) additionally clear D on reset, while the NMOS part
// leaves D unchanged; and the reset sequence performs no bus writes.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include <6502/6502.h>

namespace {

// Flat 64K memory for the bare CPU.
std::array<uint8_t, 65536> g_mem;

constexpr uint16_t kResetTarget = 0x1234; // via FFFC/FFFD
constexpr uint16_t kIrqTarget = 0x5678;   // via FFFE/FFFF
constexpr uint16_t kNmiTarget = 0x9abc;   // via FFFA/FFFB

constexpr uint8_t kIrqMask = 0x01;

// Lay out vectors and default program bytes. Everything defaults to NOP so a
// stray fetch cannot wander into an undefined opcode; the tests overwrite the
// bytes they care about.
void setup_memory() {
    g_mem.fill(0xea); // NOP

    g_mem[0xfffc] = kResetTarget & 0xff;
    g_mem[0xfffd] = kResetTarget >> 8;
    g_mem[0xfffe] = kIrqTarget & 0xff;
    g_mem[0xffff] = kIrqTarget >> 8;
    g_mem[0xfffa] = kNmiTarget & 0xff;
    g_mem[0xfffb] = kNmiTarget >> 8;

    g_mem[kResetTarget] = 0xea; // NOP
    g_mem[kIrqTarget] = 0x40;   // RTI
    g_mem[kNmiTarget] = 0x40;   // RTI
}

// A CPU that has been initialised for CONFIG and re-armed for a reset from a
// known stack pointer, with the caller free to set flags before the sequence
// runs (M6502_Reset does not clear registers, unlike M6502_Init).
M6502 make_reset_cpu(const M6502Config *config, bool irq_asserted) {
    M6502 cpu;
    M6502_Init(&cpu, config);
    if (irq_asserted) {
        M6502_SetDeviceIRQ(&cpu, kIrqMask, 1);
    }
    cpu.s.w = 0x01ff; // known SP so the reset decrements are measurable
    M6502_Reset(&cpu);
    return cpu;
}

// Drive the CPU one cycle at a time, servicing the flat memory, until the
// first opcode fetch (read == Opcode). Returns that fetch address, and counts
// any bus writes seen along the way. Interrupt-vector reads (read ==
// Interrupt) are NOT opcode fetches, so a machine that wrongly takes an
// interrupt out of reset returns the IRQ handler address here.
uint16_t drive_to_first_opcode(M6502 &cpu, int &writes, size_t max_cycles = 64) {
    writes = 0;
    for (size_t c = 0; c < max_cycles; ++c) {
        (*cpu.tfn)(&cpu);
        if (cpu.read) {
            cpu.dbus = g_mem[cpu.abus.w];
        } else {
            g_mem[cpu.abus.w] = cpu.dbus;
            ++writes;
        }
        if (M6502_IsAboutToExecute(&cpu)) {
            return cpu.abus.w;
        }
    }
    return 0xffff; // never fetched an opcode within the budget
}

// Collect the addresses of successive opcode fetches until either the IRQ
// handler is reached or the budget runs out.
std::vector<uint16_t> collect_opcode_fetches(M6502 &cpu, size_t max_cycles = 256) {
    std::vector<uint16_t> fetches;
    for (size_t c = 0; c < max_cycles; ++c) {
        (*cpu.tfn)(&cpu);
        if (cpu.read) {
            cpu.dbus = g_mem[cpu.abus.w];
        } else {
            g_mem[cpu.abus.w] = cpu.dbus;
        }
        if (M6502_IsAboutToExecute(&cpu)) {
            fetches.push_back(cpu.abus.w);
            if (cpu.abus.w == kIrqTarget) {
                break;
            }
        }
    }
    return fetches;
}

struct NamedConfig {
    const char *name;
    const M6502Config *config;
    bool cmos; // clears D on reset
};

// The host uses NMOS; the Tube coprocessors use Rockwell 65C02 (CMOS). The
// plain CMOS config is exercised too since it shares the reset path.
const NamedConfig kConfigs[] = {
    {"NMOS 6502", &M6502_nmos6502_config, false},
    {"CMOS 65C02", &M6502_cmos6502_config, true},
    {"Rockwell 65C02", &M6502_rockwell65c02_config, true},
};

} // namespace

TEST_CASE("Reset with an IRQ pending fetches the reset vector, not the IRQ vector (#78)") {
    for (const auto &nc : kConfigs) {
        SECTION(nc.name) {
            setup_memory();
            M6502 cpu = make_reset_cpu(nc.config, /*irq_asserted=*/true);

            int writes = 0;
            uint16_t first = drive_to_first_opcode(cpu, writes);

            INFO(nc.name << ": first opcode fetch at " << std::hex << first);
            CHECK(first == kResetTarget);
            CHECK(first != kIrqTarget);
        }
    }
}

TEST_CASE("Reset sets the I flag, with and without a pending IRQ (#78)") {
    for (const auto &nc : kConfigs) {
        SECTION(nc.name) {
            for (bool irq : {false, true}) {
                setup_memory();
                M6502 cpu = make_reset_cpu(nc.config, irq);
                cpu.p.bits.i = 0; // clear it first so the sequence must set it

                int writes = 0;
                drive_to_first_opcode(cpu, writes);

                INFO(nc.name << (irq ? " (IRQ pending)" : " (no IRQ)"));
                CHECK(cpu.p.bits.i == 1);
            }
        }
    }
}

TEST_CASE("A pending IRQ is masked across reset, not lost, until CLI runs (#78)") {
    for (const auto &nc : kConfigs) {
        SECTION(nc.name) {
            setup_memory();
            // Reset handler: CLI ; NOP. The IRQ must not be taken until after
            // CLI clears I, and then it must be taken (masked, not lost).
            g_mem[kResetTarget] = 0x58;     // CLI
            g_mem[kResetTarget + 1] = 0xea; // NOP

            M6502 cpu = make_reset_cpu(nc.config, /*irq_asserted=*/true);

            std::vector<uint16_t> fetches = collect_opcode_fetches(cpu);

            REQUIRE(fetches.size() >= 2);
            CHECK(fetches.front() == kResetTarget); // CLI runs first
            CHECK(fetches[1] == kResetTarget + 1);  // NOP still masked

            bool irq_taken = false;
            size_t irq_index = 0;
            for (size_t i = 0; i < fetches.size(); ++i) {
                if (fetches[i] == kIrqTarget) {
                    irq_taken = true;
                    irq_index = i;
                    break;
                }
            }
            INFO(nc.name);
            CHECK(irq_taken);       // the pending IRQ was preserved
            CHECK(irq_index >= 2);  // and only taken after CLI + one instruction
        }
    }
}

TEST_CASE("Reset performs no writes and lowers SP by three (#78 guard)") {
    for (const auto &nc : kConfigs) {
        SECTION(nc.name) {
            setup_memory();
            M6502 cpu = make_reset_cpu(nc.config, /*irq_asserted=*/false);
            uint16_t sp_before = cpu.s.w;

            int writes = 0;
            drive_to_first_opcode(cpu, writes);

            INFO(nc.name);
            CHECK(writes == 0);
            CHECK(((sp_before - cpu.s.w) & 0xff) == 3);
        }
    }
}

// A RESET is not a power-on: it preserves the registers. Sources: MCS6500
// Programming Manual s9.3 ("the only automatic operations of the microprocessor
// during reset are to turn on the interrupt disable bit and to force the program
// counter to the vector location specified in locations FFFC and FFFD");
// NESdev "CPU power up state" (https://www.nesdev.org/wiki/CPU_power_up_state),
// hardware-measured on a 6502 core: after reset A, X, Y and the C, Z, D, V, N
// flags are unchanged, I = 1; and Michael Steil's Visual6502 analysis
// (https://www.pagetable.com/?p=410): the three stack cycles are reads, so S
// ends three lower.
TEST_CASE("Reset preserves A/X/Y and the N V Z C flags across the sequence (#78 guard)") {
    for (const auto &nc : kConfigs) {
        SECTION(nc.name) {
            setup_memory();
            M6502 cpu = make_reset_cpu(nc.config, /*irq_asserted=*/false);
            cpu.a = 0x12;
            cpu.x = 0x34;
            cpu.y = 0x56;
            cpu.p.bits.n = 1;
            cpu.p.bits.v = 1;
            cpu.p.bits.z = 1;
            cpu.p.bits.c = 1;
            cpu.p.bits.d = 1;
            cpu.p.bits.i = 0;
            uint16_t sp_before = cpu.s.w;

            int writes = 0;
            drive_to_first_opcode(cpu, writes);

            INFO(nc.name);
            CHECK(cpu.a == 0x12);
            CHECK(cpu.x == 0x34);
            CHECK(cpu.y == 0x56);
            CHECK(cpu.p.bits.n == 1);
            CHECK(cpu.p.bits.v == 1);
            CHECK(cpu.p.bits.z == 1);
            CHECK(cpu.p.bits.c == 1);
            CHECK(cpu.p.bits.i == 1); // set by the sequence
            CHECK(((sp_before - cpu.s.w) & 0xff) == 3);
            if (nc.cmos) {
                CHECK(cpu.p.bits.d == 0);
            } else {
                CHECK(cpu.p.bits.d == 1);
            }
        }
    }
}

TEST_CASE("Reset clears D on CMOS parts and leaves it unchanged on NMOS (#78)") {
    for (const auto &nc : kConfigs) {
        SECTION(nc.name) {
            setup_memory();
            M6502 cpu = make_reset_cpu(nc.config, /*irq_asserted=*/false);
            cpu.p.bits.d = 1; // decimal set before the sequence runs

            int writes = 0;
            drive_to_first_opcode(cpu, writes);

            INFO(nc.name);
            if (nc.cmos) {
                CHECK(cpu.p.bits.d == 0); // CMOS initialises D = 0 on reset
            } else {
                CHECK(cpu.p.bits.d == 1); // NMOS leaves D unchanged
            }
        }
    }
}

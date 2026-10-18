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

// The B+ decides whether an instruction reaches shadow RAM at its opcode
// fetch, and holds that decision for the instruction's operand and data
// accesses (B+ Service Manual Section 2, PAL IC36). These run real STA and LDA
// instructions whose opcode sits on one side of a VDU driver boundary and
// whose operand bytes cross it, so the program counter at the data access is
// on the other side.

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <beebium/Machines.hpp>

#include <cstdint>
#include <vector>

using namespace beebium;

namespace {

constexpr uint8_t NOP = 0xEA;
constexpr uint8_t STA_ABS = 0x8D;
constexpr uint8_t LDA_ABS = 0xAD;
constexpr uint8_t SLOT = 2;            // a user socket on both B+ variants
constexpr uint8_t IN_MAIN = 0xAA;      // &3000 in main RAM
constexpr uint8_t IN_SHADOW = 0x55;    // &3000 in shadow RAM

// A B+ with all-NOP MOS and sideways images, shadow mode on, ready to run
// planted code: `code` maps addresses to bytes, placed in the MOS image
// (&C000 up), the sideways image (&8000-&BFFF) or, with ANDY paged in,
// ANDY's RAM (&8000-&AFFF).
template <typename Machine>
struct Rig {
    Machine machine;

    Rig(const std::vector<std::pair<uint16_t, uint8_t>>& code, bool andy) {
        std::vector<uint8_t> mos(0x4000, NOP);
        std::vector<uint8_t> rom(0x4000, NOP);
        for (const auto& [addr, byte] : code) {
            if (addr >= 0xC000) {
                mos[addr - 0xC000] = byte;
            } else if (!(andy && addr < 0xB000)) {
                rom[addr - 0x8000] = byte;
            }
        }
        auto& hw = machine.memory();
        hw.load_mos(mos.data(), mos.size());
        hw.load_sideways_rom(SLOT, rom.data(), rom.size(), "nops.rom");
        machine.reset();
        machine.step_instruction();  // through the reset sequence
        hw.romsel_reg.write(0, static_cast<uint8_t>((andy ? 0x80 : 0x00) | SLOT));
        hw.acccon_reg.write(0, 0x80);
        for (const auto& [addr, byte] : code) {
            if (andy && addr >= 0x8000 && addr < 0xB000) machine.write(addr, byte);
        }
        hw.main_ram.write(0x3000, IN_MAIN);
        hw.shadow_ram.write(0x0000, IN_SHADOW);
    }

    // Run the NOP before `opcode_addr`, so the instruction's own opcode fetch
    // is made by the CPU, then the instruction.
    void run_from_nop_before(uint16_t opcode_addr) {
        machine.set_pc(static_cast<uint16_t>(opcode_addr - 1));
        machine.step_instruction();
        REQUIRE(machine.pc() == opcode_addr);
        machine.step_instruction();
    }
};

// An absolute instruction with its opcode at `at`, addressing &3000, preceded
// by a NOP.
std::vector<std::pair<uint16_t, uint8_t>> at(uint16_t opcode_addr, uint8_t opcode) {
    return {{static_cast<uint16_t>(opcode_addr - 1), NOP},
            {opcode_addr, opcode},
            {static_cast<uint16_t>(opcode_addr + 1), 0x00},
            {static_cast<uint16_t>(opcode_addr + 2), 0x30}};
}

enum class Reached { Main, Shadow };

template <typename Machine>
Reached written_by_sta_at(uint16_t opcode_addr, bool andy) {
    Rig<Machine> rig(at(opcode_addr, STA_ABS), andy);
    rig.machine.set_a(0x77);
    rig.run_from_nop_before(opcode_addr);
    auto& hw = rig.machine.memory();
    const bool to_shadow = hw.shadow_ram.read(0x0000) == 0x77;
    const bool to_main = hw.main_ram.read(0x3000) == 0x77;
    REQUIRE(to_shadow != to_main);
    return to_shadow ? Reached::Shadow : Reached::Main;
}

template <typename Machine>
Reached read_by_lda_at(uint16_t opcode_addr, bool andy) {
    Rig<Machine> rig(at(opcode_addr, LDA_ABS), andy);
    rig.run_from_nop_before(opcode_addr);
    const uint8_t a = rig.machine.a();
    REQUIRE((a == IN_MAIN || a == IN_SHADOW));
    return a == IN_SHADOW ? Reached::Shadow : Reached::Main;
}

}  // namespace

TEMPLATE_TEST_CASE("The B+ routes an instruction by its opcode fetch address",
                   "[bplus][shadow][vdu]", ModelBPlus, ModelBPlus128K) {
    // Opcode in the MOS's VDU drivers, operands past &DFFF: shadow RAM.
    CHECK(written_by_sta_at<TestType>(0xDFFE, false) == Reached::Shadow);
    CHECK(read_by_lda_at<TestType>(0xDFFE, false) == Reached::Shadow);
    // Opcode in a sideways ROM, operands into the MOS at &C000: main RAM.
    CHECK(written_by_sta_at<TestType>(0xBFFE, false) == Reached::Main);
    CHECK(read_by_lda_at<TestType>(0xBFFE, false) == Reached::Main);

    // ANDY paged in. Opcode at &9FFE, operands into ANDY's VDU space at &A000:
    // main RAM.
    CHECK(written_by_sta_at<TestType>(0x9FFE, true) == Reached::Main);
    CHECK(read_by_lda_at<TestType>(0x9FFE, true) == Reached::Main);
    // Opcode in ANDY's VDU space at &AFFE, operands into the ROM at &B000:
    // shadow RAM.
    CHECK(written_by_sta_at<TestType>(0xAFFE, true) == Reached::Shadow);
    CHECK(read_by_lda_at<TestType>(0xAFFE, true) == Reached::Shadow);

    // ANDY out: &A000-&AFFF is sideways ROM, which never has VDU status.
    CHECK(written_by_sta_at<TestType>(0xAFFE, false) == Reached::Main);
    CHECK(written_by_sta_at<TestType>(0x9FFE, false) == Reached::Main);
}

TEMPLATE_TEST_CASE("The B+ VDU flag holds from one opcode fetch to the next",
                   "[bplus][shadow][vdu]", ModelBPlus, ModelBPlus128K) {
    Rig<TestType> rig(at(0xDFFE, STA_ABS), false);
    auto& hw = rig.machine.memory();
    rig.machine.set_pc(0xDFFD);
    CHECK(hw.vdu());  // setting the PC stands for the fetch at it
    rig.machine.step_instruction();  // the NOP at &DFFD, then the fetch at &DFFE
    CHECK(hw.vdu());
    rig.machine.step_instruction();  // the STA; the next fetch is at &E001
    CHECK_FALSE(hw.vdu());
}

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

// test_master_128_hardware.cpp
// The Master 128's memory map and paging rules (Master128Hardware): ROMSEL
// and ACCCON readback, ANDY, HAZEL (Y), LYNNE by X and by E with the SYNC
// rule, the display by D, TST, IRR, the stale data bus and the 1 MHz map.
// The facts are docs/discussion/master-128-research.md, section 1.

#include <beebium/Machines.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace beebium;

namespace {

using HW = Master128Hardware;

constexpr uint8_t NOP = 0xEA;
constexpr uint8_t STA_ABS = 0x8D;
constexpr uint8_t LDA_ABS = 0xAD;
constexpr uint8_t ROM_SLOT = 8;        // IC27, a ROM socket
constexpr uint8_t IN_MAIN = 0xAA;      // &3000 in main RAM
constexpr uint8_t IN_LYNNE = 0x55;     // &3000 in LYNNE

// A Master with all-NOP MOS and bank 8 images, bank 8 paged in, with
// `code` (address, byte) planted in them, and `acccon` written.
struct Rig {
    std::unique_ptr<Master128> machine = std::make_unique<Master128>();

    Rig(const std::vector<std::pair<uint16_t, uint8_t>>& code, uint8_t acccon) {
        std::vector<uint8_t> mos(0x4000, NOP);
        std::vector<uint8_t> rom(0x4000, NOP);
        for (const auto& [addr, byte] : code) {
            if (addr >= 0xC000) {
                mos[addr - 0xC000] = byte;
            } else {
                rom[addr - 0x8000] = byte;
            }
        }
        auto& hw = machine->memory();
        hw.load_mos(mos.data(), mos.size());
        hw.load_sideways_rom(ROM_SLOT, rom.data(), rom.size(), "nops.rom");
        machine->reset();
        machine->step_instruction();  // through the reset sequence
        hw.romsel_register.write(0, ROM_SLOT);
        hw.acccon_register.write(0, acccon);
        hw.main_ram.write(0x3000, IN_MAIN);
        hw.lynne.write(0x0000, IN_LYNNE);
    }

    // Run the NOP before `opcode_addr`, then the instruction there.
    void run_from_nop_before(uint16_t opcode_addr) {
        machine->set_pc(static_cast<uint16_t>(opcode_addr - 1));
        machine->step_instruction();
        REQUIRE(machine->pc() == opcode_addr);
        machine->step_instruction();
    }
};

// An absolute instruction with its opcode at `opcode_addr`, addressing
// &3000, preceded by a NOP.
std::vector<std::pair<uint16_t, uint8_t>> at(uint16_t opcode_addr, uint8_t opcode) {
    return {{static_cast<uint16_t>(opcode_addr - 1), NOP},
            {opcode_addr, opcode},
            {static_cast<uint16_t>(opcode_addr + 1), 0x00},
            {static_cast<uint16_t>(opcode_addr + 2), 0x30}};
}

enum class Reached { Main, Lynne };

Reached written_by_sta_at(uint16_t opcode_addr, uint8_t acccon) {
    Rig rig(at(opcode_addr, STA_ABS), acccon);
    rig.machine->set_a(0x77);
    rig.run_from_nop_before(opcode_addr);
    auto& hw = rig.machine->memory();
    const bool to_lynne = hw.lynne.read(0x0000) == 0x77;
    const bool to_main = hw.main_ram.read(0x3000) == 0x77;
    REQUIRE(to_lynne != to_main);
    return to_lynne ? Reached::Lynne : Reached::Main;
}

Reached read_by_lda_at(uint16_t opcode_addr, uint8_t acccon) {
    Rig rig(at(opcode_addr, LDA_ABS), acccon);
    rig.run_from_nop_before(opcode_addr);
    const uint8_t a = rig.machine->a();
    REQUIRE((a == IN_MAIN || a == IN_LYNNE));
    return a == IN_LYNNE ? Reached::Lynne : Reached::Main;
}

// A CRTC output naming one character address and raster.
struct VideoFetch {
    uint16_t address;
    uint8_t raster;
};

}  // namespace

TEST_CASE("Master 128 identity", "[master128][hardware]") {
    CHECK(HW::MACHINE_TYPE == "master-128");
    CHECK(HW::MACHINE_DISPLAY_NAME == "BBC Master 128");
    CHECK(HW::DEFAULT_MOS_ROM == "acorn-mos_3_20.rom");
    CHECK(HW::DEFAULT_LANGUAGE_SLOT == 12);
    CHECK(HW::DEFAULT_LANGUAGE_ROM == "bbc-basic_4.rom");
    CHECK(HW::DEFAULT_SIDEWAYS_ROMS.size() == 6);
}

TEST_CASE("Master 128 sideways topology: cartridges, RAM 4-7, IC27, the MOS ROM's banks",
          "[master128][hardware][sideways]") {
    const auto topo = HW::slot_topology();
    REQUIRE(topo.sockets.size() == 16);
    for (int slot = 0; slot < 16; ++slot) {
        const auto& s = topo.sockets[static_cast<size_t>(slot)];
        INFO("slot " << slot);
        if (slot >= 4 && slot <= 7) {
            CHECK(s.supports_ram);
            CHECK_FALSE(s.supports_rom);
            CHECK_FALSE(s.supports_empty);
        } else if (slot >= 9) {
            CHECK(s.supports_rom);
            CHECK_FALSE(s.supports_ram);
            CHECK_FALSE(s.supports_empty);
        } else {
            CHECK(s.supports_rom);
            CHECK(s.supports_empty);
        }
    }
    auto hw = std::make_unique<HW>();
    for (uint8_t slot = 4; slot <= 7; ++slot) CHECK(hw->sideways.bank_type(slot) == SlotType::Ram);
}

TEST_CASE("Master 128 ROMSEL reads back what was written, at all four addresses",
          "[master128][hardware][romsel]") {
    auto hw = std::make_unique<HW>();
    hw->write(0xFE30, 0x8C);
    CHECK(hw->read(0xFE30) == 0x8C);
    CHECK(hw->read(0xFE33) == 0x8C);
    CHECK(hw->sideways.selected_bank() == 12);
    CHECK(hw->andy_paged());
    // Bits 4-6 read back as written (an assumption: research section 5 item 2).
    hw->write(0xFE31, 0x75);
    CHECK(hw->read(0xFE32) == 0x75);
    CHECK(hw->sideways.selected_bank() == 5);
    CHECK_FALSE(hw->andy_paged());
}

TEST_CASE("Master 128 ACCCON keeps all eight bits and reads them back",
          "[master128][hardware][acccon]") {
    auto hw = std::make_unique<HW>();
    // TST clear: with it set, reads of &FC00-&FFFF return the MOS ROM.
    for (uint8_t value : {uint8_t{0x00}, uint8_t{0xBF}, uint8_t{0x9A}, uint8_t{0xA5}}) {
        hw->write(0xFE34, value);
        CHECK(hw->read(0xFE34) == value);
        CHECK(hw->read(0xFE37) == value);
        CHECK(hw->acccon() == value);
    }
}

TEST_CASE("Master 128 ANDY covers &8000-&8FFF only while ROMSEL bit 7 is set",
          "[master128][hardware][andy]") {
    auto hw = std::make_unique<HW>();
    std::vector<uint8_t> rom(0x4000, 0x11);
    hw->load_sideways_rom(ROM_SLOT, rom.data(), rom.size(), "r.rom");
    hw->write(0xFE30, 0x80 | ROM_SLOT);
    hw->write(0x8000, 0x42);
    hw->write(0x8FFF, 0x43);
    CHECK(hw->read(0x8000) == 0x42);
    CHECK(hw->read(0x8FFF) == 0x43);
    CHECK(hw->read(0x9000) == 0x11);  // the bank above ANDY
    CHECK(hw->andy.read(0x0000) == 0x42);
    hw->write(0xFE30, ROM_SLOT);
    CHECK(hw->read(0x8000) == 0x11);
}

TEST_CASE("Master 128 Y pages HAZEL over &C000-&DFFF", "[master128][hardware][hazel]") {
    auto hw = std::make_unique<HW>();
    std::vector<uint8_t> mos(0x4000, 0x99);
    hw->load_mos(mos.data(), mos.size());
    hw->write(0xC000, 0x12);  // Y clear: a write to the ROM goes nowhere
    CHECK(hw->read(0xC000) == 0x99);
    CHECK(hw->hazel.read(0x0000) == 0x00);
    hw->write(0xFE34, HW::ACCCON_Y);
    hw->write(0xC000, 0x12);
    hw->write(0xDFFF, 0x13);
    CHECK(hw->read(0xC000) == 0x12);
    CHECK(hw->read(0xDFFF) == 0x13);
    CHECK(hw->read(0xE000) == 0x99);  // the MOS above HAZEL
    hw->write(0xFE34, 0x00);
    CHECK(hw->read(0xC000) == 0x99);
    CHECK(hw->hazel.read(0x0000) == 0x12);
}

TEST_CASE("Master 128 X sends every access to &3000-&7FFF to LYNNE",
          "[master128][hardware][shadow]") {
    // From user code and from the VDU driver's window alike.
    CHECK(written_by_sta_at(0x8100, HW::ACCCON_X) == Reached::Lynne);
    CHECK(read_by_lda_at(0x8100, HW::ACCCON_X) == Reached::Lynne);
    CHECK(written_by_sta_at(0xC100, HW::ACCCON_X) == Reached::Lynne);
    CHECK(written_by_sta_at(0xE100, HW::ACCCON_X) == Reached::Lynne);
    // A debugger's access with no instruction behind it: X alone.
    auto hw = std::make_unique<HW>();
    hw->lynne.write(0x1000, 0x66);
    CHECK(hw->read(0x4000) == 0x00);
    hw->write(0xFE34, HW::ACCCON_X);
    CHECK(hw->read(0x4000) == 0x66);
}

TEST_CASE("Master 128 E redirects an instruction fetched from &C000-&DFFF to LYNNE",
          "[master128][hardware][shadow][sync]") {
    const uint8_t e = HW::ACCCON_E;
    // The window's edges, decided by the opcode fetch's address.
    CHECK(written_by_sta_at(0xBFFF, e) == Reached::Main);   // operands in &C000
    CHECK(written_by_sta_at(0xC000, e) == Reached::Lynne);
    CHECK(written_by_sta_at(0xDFFF, e) == Reached::Lynne);  // operands past &DFFF
    CHECK(written_by_sta_at(0xE000, e) == Reached::Main);
    CHECK(read_by_lda_at(0xBFFF, e) == Reached::Main);
    CHECK(read_by_lda_at(0xC000, e) == Reached::Lynne);
    CHECK(read_by_lda_at(0xDFFF, e) == Reached::Lynne);
    CHECK(read_by_lda_at(0xE000, e) == Reached::Main);
    // E clear: the window counts for nothing.
    CHECK(written_by_sta_at(0xC100, 0x00) == Reached::Main);
}

TEST_CASE("Master 128 E is decided again at the next opcode fetch",
          "[master128][hardware][shadow][sync]") {
    // STA &3000 fetched at &DFFC, in the window, then a NOP and LDA &3000
    // fetched from &E000, outside it.
    Rig rig({{0xDFFC, STA_ABS}, {0xDFFD, 0x00}, {0xDFFE, 0x30},
             {0xDFFF, NOP},
             {0xE000, LDA_ABS}, {0xE001, 0x00}, {0xE002, 0x30}},
            HW::ACCCON_E);
    rig.machine->set_a(0x77);
    rig.machine->set_pc(0xDFFC);
    rig.machine->step_instruction();  // STA, fetched in the window
    auto& hw = rig.machine->memory();
    CHECK(hw.lynne.read(0x0000) == 0x77);
    CHECK(hw.main_ram.read(0x3000) == IN_MAIN);
    rig.machine->step_instruction();  // NOP at &DFFF
    rig.machine->step_instruction();  // LDA, fetched from &E000
    CHECK_FALSE(hw.vdu());
    CHECK(rig.machine->a() == IN_MAIN);
}

TEST_CASE("Master 128 X set with E clear: the VDU driver's writes land in LYNNE",
          "[master128][hardware][shadow][planet_nubium]") {
    // The Planet Nubium corner (research section 1.1): X wins.
    CHECK(written_by_sta_at(0xC100, HW::ACCCON_X) == Reached::Lynne);
    CHECK(read_by_lda_at(0xC100, HW::ACCCON_X) == Reached::Lynne);
}

TEST_CASE("Master 128 Y leaves E alone for code in HAZEL's window",
          "[master128][hardware][shadow]") {
    // Acorn describes E without Y; Y_SUPPRESSES_E is the assumption to flip
    // if the hardware says otherwise (research section 5 item 1).
    STATIC_REQUIRE_FALSE(HW::Y_SUPPRESSES_E);
    auto hw = std::make_unique<HW>();
    hw->write(0xFE34, HW::ACCCON_E | HW::ACCCON_Y);
    CHECK(hw->lynne_selected(/*vdu=*/true));
    CHECK_FALSE(hw->lynne_selected(/*vdu=*/false));
}

TEST_CASE("Master 128 D shows LYNNE, and ANDY and HAZEL below &3000",
          "[master128][hardware][display]") {
    auto hw = std::make_unique<HW>();
    hw->main_ram.write(0x0500, 0x01);
    hw->main_ram.write(0x1500, 0x02);
    hw->main_ram.write(0x3500, 0x03);
    hw->andy.write(0x0500, 0x11);
    hw->hazel.write(0x0500, 0x12);
    hw->lynne.write(0x0500, 0x13);
    CHECK(hw->peek_video(0x0500) == 0x01);
    CHECK(hw->peek_video(0x1500) == 0x02);
    CHECK(hw->peek_video(0x3500) == 0x03);
    CHECK(hw->video_region() == HW::REGION_MAIN_RAM);
    hw->write(0xFE34, HW::ACCCON_D);
    CHECK(hw->peek_video(0x0500) == 0x11);
    CHECK(hw->peek_video(0x1500) == 0x12);
    CHECK(hw->peek_video(0x3500) == 0x13);
    CHECK(hw->video_region() == HW::REGION_LYNNE);
    // D is the display alone: the CPU still sees main RAM.
    CHECK(hw->read(0x3500) == 0x03);
}

TEST_CASE("Master 128 TST makes reads of &FC00-&FFFF return the MOS ROM",
          "[master128][hardware][tst]") {
    // The emulators' consensus, unmeasured (research section 5 item 10).
    auto hw = std::make_unique<HW>();
    std::vector<uint8_t> mos(0x4000, 0x00);
    mos[0xFE34 - 0xC000] = 0xC3;
    mos[0xFC00 - 0xC000] = 0xC4;
    hw->load_mos(mos.data(), mos.size());
    hw->write(0xFE34, HW::ACCCON_TST);
    CHECK(hw->read(0xFE34) == 0xC3);
    CHECK(hw->read(0xFC00) == 0xC4);
    // Writes still reach the I/O: ACCCON takes this one.
    hw->write(0xFE34, 0x00);
    CHECK(hw->read(0xFE34) == 0x00);
}

TEST_CASE("Master 128 IRR holds the IRQ line low while set", "[master128][hardware][irr]") {
    auto hw = std::make_unique<HW>();
    CHECK(hw->poll_irq() == 0);
    hw->write(0xFE34, HW::ACCCON_IRR);
    CHECK(hw->poll_irq() != 0);
    CHECK(hw->poll_irq() != 0);  // a level, not an edge
    hw->write(0xFE34, 0x00);
    CHECK(hw->poll_irq() == 0);
}

TEST_CASE("Master 128 unmapped SHEILA and empty banks read as the last video fetch",
          "[master128][hardware][stale_bus]") {
    auto hw = std::make_unique<HW>();
    // A Mode 7 character address: the CRTC's MA13 maps &2000 to &7C00.
    hw->main_ram.write(0x7C00, 0x20);
    hw->note_video_fetch(VideoFetch{0x2000, 0});
    CHECK(hw->stale_bus() == 0x20);
    for (uint16_t addr : {uint16_t{0xFE24}, uint16_t{0xFE2F}, uint16_t{0xFE38}, uint16_t{0xFE3C},
                          uint16_t{0xFE80}, uint16_t{0xFE9F}, uint16_t{0xFEC0}, uint16_t{0xFEDF},
                          uint16_t{0xFEA0}, uint16_t{0xFEE0}}) {
        INFO("address " << std::hex << addr);
        CHECK(hw->read(addr) == 0x20);
    }
    hw->write(0xFE30, 0);  // bank 0, an empty cartridge slot
    CHECK(hw->read(0x8000) == 0x20);
    CHECK(hw->read(0xBFFF) == 0x20);
    // In Mode 0 the bus holds what the screen holds.
    hw->main_ram.write(0x3000, 0x00);
    hw->note_video_fetch(VideoFetch{0x0600, 0});  // MA &0600 x 8 = &3000
    CHECK(hw->stale_bus() == 0x00);
    CHECK(hw->read(0xFEC0) == 0x00);
}

TEST_CASE("Master 128 stretches the 1 MHz devices, and FRED and JIM while IFJ is clear",
          "[master128][hardware][stretch]") {
    auto hw = std::make_unique<HW>();
    for (uint16_t addr : {uint16_t{0xFE00}, uint16_t{0xFE1F}, uint16_t{0xFE28}, uint16_t{0xFE2B},
                          uint16_t{0xFE40}, uint16_t{0xFE7F}, uint16_t{0xFE80}, uint16_t{0xFE9F},
                          uint16_t{0xFC00}, uint16_t{0xFDFF}}) {
        INFO("address " << std::hex << addr);
        CHECK(hw->is_1mhz_access(addr));
    }
    for (uint16_t addr : {uint16_t{0xFE20}, uint16_t{0xFE24}, uint16_t{0xFE2C}, uint16_t{0xFE30},
                          uint16_t{0xFE34}, uint16_t{0xFE38}, uint16_t{0xFEA0}, uint16_t{0xFEC0},
                          uint16_t{0xFEE0}, uint16_t{0xFF00}, uint16_t{0x3000}, uint16_t{0xC000}}) {
        INFO("address " << std::hex << addr);
        CHECK_FALSE(hw->is_1mhz_access(addr));
    }
    hw->write(0xFE34, HW::ACCCON_IFJ);
    CHECK_FALSE(hw->is_1mhz_access(0xFC00));
    CHECK_FALSE(hw->is_1mhz_access(0xFDFF));
}

TEST_CASE("Master 128 Break clears ROMSEL and ACCCON", "[master128][hardware][reset]") {
    auto hw = std::make_unique<HW>();
    hw->write(0xFE30, 0x8F);
    hw->write(0xFE34, 0xFF);
    hw->soft_reset();
    CHECK(hw->romsel() == 0);
    CHECK(hw->acccon() == 0);
}

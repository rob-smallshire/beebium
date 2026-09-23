// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

// Tests for the BBC Model B fitted with the Computech Integra-B expansion board.
// References: docs/integra-b (IBOS 1.20 User Guide sections 8-2, 9-4, 9-6 and
// the reverse-engineered schematic).

#include <catch2/catch_test_macros.hpp>
#include <beebium/ModelBIntegraBHardware.hpp>
#include <beebium/Machines.hpp>
#include <beebium/SlotTopology.hpp>

#include <algorithm>
#include <array>
#include <string>

using namespace beebium;

namespace {

constexpr uint8_t PRVEN = 0x40;
constexpr uint8_t MEMSEL = 0x80;
constexpr uint8_t PRVS8 = 0x10;
constexpr uint8_t PRVS4 = 0x20;
constexpr uint8_t PRVS1 = 0x40;
constexpr uint8_t SHEN = 0x80;

std::array<uint8_t, 16384> make_image(uint8_t fill) {
    std::array<uint8_t, 16384> img;
    img.fill(fill);
    return img;
}

const SlotProtectionGroup* find_group(const std::vector<SlotProtectionGroup>& groups,
                                      std::string_view id) {
    auto it = std::find_if(groups.begin(), groups.end(),
                           [&](const SlotProtectionGroup& g) { return g.id == id; });
    return it == groups.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("Integra-B identity and defaults", "[hardware][integra_b]") {
    CHECK(ModelBIntegraBHardware::MACHINE_TYPE == "model-b-integra-b");
    CHECK(ModelBIntegraBHardware::DEFAULT_BOARD_ROM == "computech-ibos_1_26.rom");
    CHECK(ModelBIntegraBHardware::DEFAULT_BOARD_ROM_SLOT == 15);
    CHECK(ModelBIntegraBHardware::DEFAULT_LANGUAGE_SLOT == 3);

    ModelBIntegraBHardware hw;
    SECTION("the board's four sideways RAM banks 4-7 are always fitted") {
        for (uint8_t slot = 4; slot <= 7; ++slot) {
            CHECK(hw.sideways.bank_type(slot) == SlotType::Ram);
        }
    }
    SECTION("the socket pairs 8-15 start empty") {
        for (uint8_t slot = 8; slot <= 15; ++slot) {
            CHECK(hw.sideways.bank_type(slot) == SlotType::Empty);
        }
    }
}

TEST_CASE("Integra-B ROMSEL decodes all four bank bits", "[hardware][integra_b][romsel]") {
    ModelBIntegraBHardware hw;
    auto low = make_image(0x11);
    auto high = make_image(0xDD);
    hw.load_rom_to_slot(1, low.data(), low.size());
    hw.load_rom_to_slot(13, high.data(), high.size());

    hw.write(0xFE30, 13);
    CHECK(hw.read(0x8000) == 0xDD);
    hw.write(0xFE30, 1);
    CHECK(hw.read(0x8000) == 0x11);
}

TEST_CASE("Integra-B latches are mirrored across four addresses", "[hardware][integra_b][romsel]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE33, static_cast<uint8_t>(MEMSEL | PRVEN | 5));
    CHECK(hw.romsel() == (MEMSEL | PRVEN | 5));
    CHECK(hw.sideways.selected_bank() == 5);

    hw.write(0xFE37, SHEN | PRVS4);
    CHECK(hw.ramsel() == (SHEN | PRVS4));
}

TEST_CASE("Integra-B RAMSEL latches only its four control bits", "[hardware][integra_b][ramsel]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE34, 0xFF);
    CHECK(hw.ramsel() == (SHEN | PRVS1 | PRVS4 | PRVS8));
}

TEST_CASE("Integra-B latches are write-only", "[hardware][integra_b]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE30, 3);
    hw.write(0xFE34, SHEN);
    CHECK(hw.read(0xFE30) == 0xFF);
    CHECK(hw.read(0xFE34) == 0xFF);
}

TEST_CASE("Integra-B shadow RAM selection (SHEN, MEMSEL)", "[hardware][integra_b][shadow]") {
    ModelBIntegraBHardware hw;
    hw.write(0x3000, 0x11);  // main RAM
    hw.write(0x7FFF, 0x12);

    SECTION("SHEN clear: main memory, whatever MEMSEL") {
        hw.write(0xFE30, MEMSEL);
        hw.write(0xFE34, 0);
        CHECK(hw.read(0x3000) == 0x11);
        hw.write(0xFE30, 0);
        CHECK(hw.read(0x3000) == 0x11);
    }

    SECTION("SHEN set, MEMSEL clear: shadow memory") {
        hw.write(0xFE34, SHEN);
        hw.write(0xFE30, 0);
        hw.write(0x3000, 0x21);
        hw.write(0x7FFF, 0x22);
        CHECK(hw.read(0x3000) == 0x21);
        CHECK(hw.read(0x7FFF) == 0x22);
        // Main memory is untouched by shadow writes.
        CHECK(hw.main_ram.read(0x3000) == 0x11);
        CHECK(hw.main_ram.read(0x7FFF) == 0x12);

        SECTION("SHEN set, MEMSEL set: main memory again") {
            hw.write(0xFE30, MEMSEL);
            CHECK(hw.read(0x3000) == 0x11);
            CHECK(hw.read(0x7FFF) == 0x12);
        }
    }

    SECTION("shadow never affects memory below &3000") {
        hw.write(0xFE34, SHEN);
        hw.write(0x2FFF, 0x33);
        CHECK(hw.main_ram.read(0x2FFF) == 0x33);
    }
}

TEST_CASE("Integra-B video always reads main memory", "[hardware][integra_b][shadow]") {
    ModelBIntegraBHardware hw;
    hw.write(0x5800, 0x44);
    hw.write(0xFE34, SHEN);
    hw.write(0x5800, 0x55);  // shadow
    CHECK(hw.read(0x5800) == 0x55);
    CHECK(hw.peek_video(0x5800) == 0x44);
}

TEST_CASE("Integra-B private RAM truth table (IBOS guide 8-2)", "[hardware][integra_b][private]") {
    ModelBIntegraBHardware hw;
    auto swr = make_image(0x5E);
    hw.load_sideways_data(4, swr.data(), swr.size());
    hw.write(0xFE30, 4);

    // Mark every private RAM byte so an overlay is visible.
    hw.write(0xFE30, static_cast<uint8_t>(PRVEN | 4));
    hw.write(0xFE34, PRVS1 | PRVS4 | PRVS8);
    for (uint16_t a = 0x8000; a < 0xB000; ++a) hw.write(a, 0xA7);

    struct Row {
        bool prven;
        uint8_t ramsel;
        bool p8000;  // &8000-&83FF
        bool p8400;  // &8400-&8FFF
        bool p9000;  // &9000-&AFFF
    };
    const Row rows[] = {
        {false, PRVS1 | PRVS4 | PRVS8, false, false, false},
        {true, 0, false, false, false},
        {true, PRVS1, true, false, false},
        {true, PRVS4, true, true, false},
        {true, PRVS1 | PRVS4, true, true, false},
        {true, PRVS8, false, false, true},
        {true, PRVS1 | PRVS8, true, false, true},
        {true, PRVS4 | PRVS8, true, true, true},
        {true, PRVS1 | PRVS4 | PRVS8, true, true, true},
    };
    for (const auto& row : rows) {
        hw.write(0xFE30, static_cast<uint8_t>((row.prven ? PRVEN : 0) | 4));
        hw.write(0xFE34, row.ramsel);
        INFO("PRVEN=" << row.prven << " RAMSEL=" << static_cast<int>(row.ramsel));
        CHECK((hw.read(0x8000) == 0xA7) == row.p8000);
        CHECK((hw.read(0x83FF) == 0xA7) == row.p8000);
        CHECK((hw.read(0x8400) == 0xA7) == row.p8400);
        CHECK((hw.read(0x8FFF) == 0xA7) == row.p8400);
        CHECK((hw.read(0x9000) == 0xA7) == row.p9000);
        CHECK((hw.read(0xAFFF) == 0xA7) == row.p9000);
        CHECK(hw.read(0xB000) == 0x5E);  // never private
    }
}

TEST_CASE("Integra-B private RAM overlays without disturbing sideways RAM",
          "[hardware][integra_b][private]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE30, 5);
    hw.write(0x8000, 0x05);  // sideways RAM bank 5

    hw.write(0xFE30, static_cast<uint8_t>(PRVEN | 5));
    hw.write(0xFE34, PRVS1);
    hw.write(0x8000, 0x99);  // private RAM
    CHECK(hw.read(0x8000) == 0x99);

    hw.write(0xFE30, 5);  // PRVEN clear, as the MOS leaves it
    CHECK(hw.read(0x8000) == 0x05);
    CHECK(hw.private_ram_peek(0x8000) == 0x99);
}

TEST_CASE("Integra-B private and shadow RAM survive resets (battery backed)",
          "[hardware][integra_b][reset]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE34, SHEN);
    hw.write(0x4000, 0x77);
    hw.write(0xFE30, PRVEN);
    hw.write(0xFE34, PRVS8);
    hw.write(0x9000, 0x66);
    hw.write(0xFE30, 6);
    hw.write(0x8123, 0x88);  // sideways RAM bank 6

    SECTION("soft reset (Break)") { hw.soft_reset(); }
    SECTION("hard reset (power-on)") { hw.reset(); }

    CHECK(hw.shadow_ram_peek(0x4000) == 0x77);
    CHECK(hw.private_ram_peek(0x9000) == 0x66);
    CHECK(hw.sideways.peek_bank(6, 0x0123) == 0x88);
}

TEST_CASE("Integra-B latches are cleared by the reset line", "[hardware][integra_b][reset]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE30, static_cast<uint8_t>(MEMSEL | PRVEN | 9));
    hw.write(0xFE34, SHEN | PRVS1);

    SECTION("soft reset (Break)") { hw.soft_reset(); }
    SECTION("hard reset (power-on)") { hw.reset(); }

    CHECK(hw.romsel() == 0);
    CHECK(hw.ramsel() == 0);
    CHECK(hw.sideways.selected_bank() == 0);
}

TEST_CASE("Integra-B RTC is at &FE38 (address) and &FE3C (data)", "[hardware][integra_b][rtc]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE38, 0x20);
    hw.write(0xFE3C, 0x5A);
    CHECK(hw.rtc.peek_register(0x20) == 0x5A);

    // Mirrors: &FE38-&FE3B and &FE3C-&FE3F.
    hw.write(0xFE3B, 0x21);
    hw.write(0xFE3F, 0xA5);
    CHECK(hw.rtc.peek_register(0x21) == 0xA5);
    hw.write(0xFE39, 0x20);
    CHECK(hw.read(0xFE3D) == 0x5A);
}

TEST_CASE("Integra-B RTC interrupts reach the CPU IRQ line", "[hardware][integra_b][rtc]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE38, Mc146818Rtc::REG_A);
    hw.write(0xFE3C, Mc146818Rtc::A_DV_32768HZ | 0x03);  // 122 us periodic rate
    hw.write(0xFE38, Mc146818Rtc::REG_B);
    hw.write(0xFE3C, Mc146818Rtc::B_PIE | Mc146818Rtc::B_24_HOUR);
    hw.write(0xFE38, Mc146818Rtc::REG_C);
    (void)hw.read(0xFE3C);

    for (int i = 0; i < 1000; ++i) hw.tick_expansion_devices();
    CHECK(hw.poll_irq() != 0);

    hw.write(0xFE38, Mc146818Rtc::REG_C);
    (void)hw.read(0xFE3C);
    CHECK(hw.poll_irq() == 0);
}

TEST_CASE("Integra-B reset line resets the RTC interrupt state", "[hardware][integra_b][rtc]") {
    ModelBIntegraBHardware hw;
    hw.write(0xFE38, Mc146818Rtc::REG_A);
    hw.write(0xFE3C, Mc146818Rtc::A_DV_32768HZ | 0x03);
    hw.write(0xFE38, Mc146818Rtc::REG_B);
    hw.write(0xFE3C, Mc146818Rtc::B_PIE | Mc146818Rtc::B_24_HOUR);
    for (int i = 0; i < 1000; ++i) hw.tick_expansion_devices();
    REQUIRE(hw.poll_irq() != 0);

    hw.soft_reset();
    CHECK(hw.poll_irq() == 0);
    CHECK((hw.rtc.peek_register(Mc146818Rtc::REG_B) & Mc146818Rtc::B_PIE) == 0);
}

TEST_CASE("Integra-B topology", "[hardware][integra_b][topology]") {
    auto topo = ModelBIntegraBHardware::slot_topology();
    CHECK_FALSE(topo.has_aliasing);
    REQUIRE(topo.sockets.size() == 16);

    for (int slot = 0; slot <= 3; ++slot) {
        const auto* s = topo.find_socket_for_slot(slot);
        REQUIRE(s != nullptr);
        CHECK(s->supports_rom);
        CHECK_FALSE(s->supports_ram);
        CHECK(s->supports_empty);
    }
    for (int slot = 4; slot <= 7; ++slot) {
        const auto* s = topo.find_socket_for_slot(slot);
        REQUIRE(s != nullptr);
        CHECK(s->supports_ram);
        CHECK_FALSE(s->supports_rom);
        CHECK_FALSE(s->supports_empty);
    }
    for (int slot = 8; slot <= 15; ++slot) {
        const auto* s = topo.find_socket_for_slot(slot);
        REQUIRE(s != nullptr);
        CHECK(s->supports_rom);
        CHECK(s->supports_ram);
        CHECK(s->supports_empty);
    }
    for (const auto& s : topo.sockets) {
        // Write protection is per chip (two slots), not per slot.
        CHECK_FALSE(s.supports_write_protect);
        CHECK_FALSE(s.runtime_configurable);
    }
    CHECK(topo.ram_chips ==
          std::vector<std::vector<int>>{{8, 9}, {10, 11}, {12, 13}, {14, 15}});
}

TEST_CASE("Integra-B write-protect groups are per RAM chip", "[hardware][integra_b][protection]") {
    ModelBIntegraBHardware hw;

    SECTION("the on-board chips are always present") {
        auto groups = hw.protection_groups();
        REQUIRE(groups.size() == 2);
        const auto* g45 = find_group(groups, "slots-4-5");
        REQUIRE(g45 != nullptr);
        CHECK(g45->label == "Write-protect slots 4/5 (WP 4/5)");
        CHECK(g45->slots == std::vector<int>{4, 5});
        CHECK(g45->supports_write_protect);
        CHECK_FALSE(g45->write_protected);
        CHECK_FALSE(g45->supports_hide);
        REQUIRE(find_group(groups, "slots-6-7") != nullptr);
    }

    SECTION("a socket pair fitted with RAM gains a group") {
        hw.configure_slot_as_ram(10);
        hw.configure_slot_as_ram(11);
        auto groups = hw.protection_groups();
        REQUIRE(groups.size() == 3);
        const auto* g = find_group(groups, "slots-10-11");
        REQUIRE(g != nullptr);
        CHECK(g->label == "Write-protect slots 10/11");
        CHECK(g->slots == std::vector<int>{10, 11});
    }

    SECTION("write-protecting a chip blocks writes to both its slots") {
        hw.write(0xFE30, 4);
        hw.write(0x8000, 0x44);
        hw.write(0xFE30, 5);
        hw.write(0x8000, 0x55);

        REQUIRE(hw.set_protection("slots-4-5", ProtectionKind::WriteProtect, true));
        CHECK(find_group(hw.protection_groups(), "slots-4-5")->write_protected);

        hw.write(0xFE30, 4);
        hw.write(0x8000, 0xEE);
        CHECK(hw.read(0x8000) == 0x44);
        hw.write(0xFE30, 5);
        hw.write(0x8000, 0xEE);
        CHECK(hw.read(0x8000) == 0x55);

        // The other chip is unaffected.
        hw.write(0xFE30, 6);
        hw.write(0x8000, 0x66);
        CHECK(hw.read(0x8000) == 0x66);

        REQUIRE(hw.set_protection("slots-4-5", ProtectionKind::WriteProtect, false));
        hw.write(0xFE30, 4);
        hw.write(0x8000, 0xEE);
        CHECK(hw.read(0x8000) == 0xEE);
    }

    SECTION("private RAM is a separate chip, unaffected by write protection") {
        REQUIRE(hw.set_protection("slots-4-5", ProtectionKind::WriteProtect, true));
        hw.write(0xFE30, static_cast<uint8_t>(PRVEN | 4));
        hw.write(0xFE34, PRVS1);
        hw.write(0x8000, 0x12);
        CHECK(hw.read(0x8000) == 0x12);
    }

    SECTION("unknown groups and kinds are refused") {
        CHECK_FALSE(hw.set_protection("slots-8-9", ProtectionKind::WriteProtect, true));
        CHECK_FALSE(hw.set_protection("slots-4-5", ProtectionKind::Hide, true));
        CHECK_FALSE(hw.set_protection("board", ProtectionKind::WriteProtect, true));
    }

    SECTION("write protection survives resets (it is a switch)") {
        REQUIRE(hw.set_protection("slots-6-7", ProtectionKind::WriteProtect, true));
        hw.reset();
        CHECK(find_group(hw.protection_groups(), "slots-6-7")->write_protected);
    }
}

TEST_CASE("Integra-B exposes shadow and private RAM as memory regions",
          "[hardware][integra_b][regions]") {
    ModelBIntegraBHardware hw;
    REQUIRE(hw.has_region("shadow_ram"));
    REQUIRE(hw.has_region("private_ram"));

    hw.write_region("shadow_ram", 0x3000, 0x31);
    hw.write_region("private_ram", 0xAFFF, 0x32);
    CHECK(hw.peek_region("shadow_ram", 0x3000) == 0x31);
    CHECK(hw.peek_region("private_ram", 0xAFFF) == 0x32);
    CHECK(hw.shadow_ram_peek(0x3000) == 0x31);
    CHECK(hw.private_ram_peek(0xAFFF) == 0x32);
    CHECK(hw.main_ram.read(0x3000) != 0x31);
}

TEST_CASE("ModelBIntegraB Machine instantiates", "[hardware][integra_b][machine]") {
    ModelBIntegraB machine;
    auto& hw = machine.state().memory;
    hw.write(0xFE30, 4);
    hw.write(0x8000, 0x42);
    CHECK(hw.read(0x8000) == 0x42);
    for (int i = 0; i < 100; ++i) machine.step();
}

TEST_CASE("Integra-B starts as a board that has been set up", "[hardware][integra_b][battery]") {
    ModelBIntegraBHardware hw;

    SECTION("the clock is running in IBOS's format") {
        uint8_t b = hw.rtc.peek_register(Mc146818Rtc::REG_B);
        CHECK((b & Mc146818Rtc::B_SET) == 0);
        CHECK((b & Mc146818Rtc::B_24_HOUR) != 0);
        CHECK((b & Mc146818Rtc::B_DM_BINARY) != 0);
        CHECK((hw.rtc.peek_register(Mc146818Rtc::REG_A) & Mc146818Rtc::A_DV_MASK) ==
              Mc146818Rtc::A_DV_32768HZ);
    }
    SECTION("CMOS holds IBOS's configuration, LANG pointing at BASIC in slot 3") {
        CHECK(hw.rtc.peek_register(0x13) == 0x33);
    }
    SECTION("private RAM holds IBOS's workspace") {
        CHECK(hw.private_ram_peek(0x83B2) == 0x04);  // OSMODE 4
        CHECK(hw.private_ram_peek(0x83FF) == 0x0F);  // RAM in banks 4-7
    }
    SECTION("the seed survives a power-on reset") {
        hw.reset();
        CHECK(hw.rtc.peek_register(0x13) == 0x33);
        CHECK(hw.private_ram_peek(0x83B2) == 0x04);
    }
}

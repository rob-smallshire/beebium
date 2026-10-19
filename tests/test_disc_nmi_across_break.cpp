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

// A disc command that completes while Break is held does not run the NMI
// handler after Break (#132).
//
// The WD1770 raises INTRQ while the CPU is halted by Break, and the CPU latches
// the edge. Break resets the controller, and the reset sequence consumes the
// latched edge, so the first instruction after Break is the reset handler.
// Before #132 the edge survived the reset: the CPU went straight to the NMI
// handler left in page &0D, which (as DFS's does) issued the next command, and
// DFS's controller probe then met a busy chip and disabled the filing system.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/disc/Acorn1770DiscController.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

using namespace beebium;

namespace {

constexpr uint16_t RESET_HANDLER = 0x0400;
constexpr uint16_t NMI_HANDLER = 0x0D00;
constexpr uint16_t NMI_COUNT = 0x0070;

// Select drive 0 (single density, controller out of reset), then STEP with no
// spin-up wait at the 6 ms rate, then spin.
constexpr std::array<uint8_t, 13> reset_handler = {
    0xA9, 0x29, 0x8D, 0x80, 0xFE,  // LDA #&29 : STA &FE80
    0xA9, 0x28, 0x8D, 0x84, 0xFE,  // LDA #&28 : STA &FE84
    0x4C, 0x0A, 0x04,              // JMP &040A
};

// Like DFS's: count the NMI, then issue the next command.
constexpr std::array<uint8_t, 8> nmi_handler = {
    0xE6, 0x70,                    // INC &70
    0xA9, 0x28, 0x8D, 0x84, 0xFE,  // LDA #&28 : STA &FE84
    0x40,                          // RTI
};

}  // namespace

TEST_CASE("A disc NMI raised while Break is held is not taken after Break (#132)",
          "[disc][acorn1770][nmi][reset]") {
    ModelB machine;
    auto controller = std::make_unique<Acorn1770DiscController>();
    auto& wd1770 = controller->wd1770();
    wd1770.set_spin_up_delay_enabled(false);
    machine.memory().disc_socket.install(std::move(controller));

    std::array<uint8_t, 16384> mos{};
    std::fill(mos.begin(), mos.end(), 0xEA);
    mos[0x3FFC] = RESET_HANDLER & 0xFF;
    mos[0x3FFD] = RESET_HANDLER >> 8;
    mos[0x3FFA] = NMI_HANDLER & 0xFF;
    mos[0x3FFB] = NMI_HANDLER >> 8;
    machine.memory().load_mos(mos.data(), mos.size());
    for (size_t i = 0; i < reset_handler.size(); ++i) {
        machine.write(static_cast<uint16_t>(RESET_HANDLER + i), reset_handler[i]);
    }
    for (size_t i = 0; i < nmi_handler.size(); ++i) {
        machine.write(static_cast<uint16_t>(NMI_HANDLER + i), nmi_handler[i]);
    }

    M6502_Reset(&machine.cpu());
    machine.resume();
    machine.run(100);
    REQUIRE(wd1770.busy());  // the command is in flight
    REQUIRE(machine.peek(NMI_COUNT) == 0);

    machine.break_down();
    machine.run(40000);  // the command completes while the CPU is held
    REQUIRE_FALSE(wd1770.busy());
    REQUIRE(machine.peek(NMI_COUNT) == 0);

    machine.break_up();
    machine.run(40);  // the reset sequence and the first few instructions

    CHECK(machine.peek(NMI_COUNT) == 0);
    CHECK(machine.cpu().pc.w >= RESET_HANDLER);
    CHECK(machine.cpu().pc.w < RESET_HANDLER + reset_handler.size());
}

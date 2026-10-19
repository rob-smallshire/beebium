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

// Machine-level tests for stepping over the breakpoints at the stopped-on
// instruction (issue #202).
//
// step_over_breakpoints_once() applies to the one instruction the machine is
// stopped on. A run with no breakpoints to step over must not leave it armed,
// and a reset must clear it: otherwise a breakpoint added later at an
// instruction reached at the same cycle count -- the first instruction after a
// power-on reset is always at cycle 7 -- is silently stepped over.
//
// A ModelB with no ROMs fitted reads a zero reset vector, and power-on clears
// RAM, so after a reset it executes BRK at &0000 (whose vector is also zero)
// every seven cycles: the same instruction at the same cycle counts, every time.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"

using namespace beebium;

namespace {

constexpr uint64_t BRK_CYCLES = 7;

void install_stop_callback(ModelB& m, int& fires) {
    m.set_breakpoint_hit_callback([&m, &fires](const BreakpointEntry&, uint16_t) {
        ++fires;
        m.pause();
    });
}

void break_at(ModelB& m, uint16_t addr) {
    m.set_breakpoint_entries({BreakpointEntry{1, addr, addr + 1u}});
}

} // namespace

TEST_CASE("A step-over armed with no breakpoints does not outlive a power-on reset (#202)",
          "[machine][breakpoint][debugger]") {
    ModelB m;
    int fires = 0;
    install_stop_callback(m, fires);

    m.reset();
    m.run(1);  // through the reset sequence to the first instruction boundary
    const auto first_boundary = m.cycle_count();

    m.step_over_breakpoints_once();
    m.run(1000);
    m.reset();
    m.run(1);
    REQUIRE(m.cycle_count() == first_boundary);

    break_at(m, m.cpu().opcode_pc.w);
    m.run(1000);
    CHECK(fires == 1);
    CHECK(m.is_paused());
    CHECK(m.cycle_count() == first_boundary);
}

TEST_CASE("A step-over skips the breakpoint the machine is stopped on, once (#202 guard)",
          "[machine][breakpoint][debugger]") {
    ModelB m;
    int fires = 0;
    install_stop_callback(m, fires);

    m.reset();
    m.run(1);
    const auto boundary = m.cycle_count();
    break_at(m, m.cpu().opcode_pc.w);

    m.step_over_breakpoints_once();
    m.run(1000);
    CHECK(fires == 1);
    CHECK(m.cycle_count() == boundary + BRK_CYCLES);
}

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

// A coprocessor's identity as its plugin reports it (#173): the CPU's short
// name and its clock, from which the core forms the coprocessor-cpu and
// coprocessor-clock name placeholders.

#include <catch2/catch_test_macros.hpp>

#include "SecondProcessor65C02Extension.hpp"
#include "beebium/ClockText.hpp"
#include "beebium/tube/Coprocessor.hpp"

using beebium::format_clock_mhz;

TEST_CASE("Clock text is whole megahertz, or the shortest decimal", "[clock-text]") {
    CHECK(format_clock_mhz(3'000'000) == "3 MHz");
    CHECK(format_clock_mhz(4'000'000) == "4 MHz");
    CHECK(format_clock_mhz(16'000'000) == "16 MHz");
    CHECK(format_clock_mhz(3'500'000) == "3.5 MHz");
    CHECK(format_clock_mhz(1'790'000) == "1.79 MHz");
    CHECK(format_clock_mhz(4'194'304) == "4.194304 MHz");
    CHECK(format_clock_mhz(500'000) == "0.5 MHz");
    CHECK(format_clock_mhz(1) == "0.000001 MHz");
    CHECK(format_clock_mhz(0) == "0 MHz");
}

TEST_CASE("A board's nominal clock comes from its timing", "[coprocessor][clock]") {
    // 2 MHz host cycles, crystal ticks per host cycle, ticks per read cycle.
    CHECK(beebium::nominal_clock_hz(beebium::BoardTiming{{6, 1}, 4, 5, 176, 1}) == 3'000'000);
    CHECK(beebium::nominal_clock_hz(beebium::BoardTiming{{2, 1}, 1, 1, 64, 1}) == 4'000'000);
    CHECK(beebium::nominal_clock_hz(beebium::BoardTiming{{3, 2}, 1, 1, 0, 1}) == 3'000'000);
}

TEST_CASE("The 6502 Second Processor reports a 65C02 at 3 MHz", "[coprocessor][identity]") {
    auto ext = beebium::SecondProcessor65C02Extension::make_65c02();
    CHECK(ext->cpu_name() == "65C02");
    CHECK(ext->clock_hz() == 3'000'000);
}

TEST_CASE("The 65C102 Co-processor reports a 65C102 at 4 MHz", "[coprocessor][identity]") {
    auto ext = beebium::SecondProcessor65C02Extension::make_65c102();
    CHECK(ext->cpu_name() == "65C102");
    CHECK(ext->clock_hz() == 4'000'000);
}

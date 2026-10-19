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

// DeviceInspection.GetRealTimeClockState reads the Integra-B's CDP6818 without
// the side effects of the guest's RTC ports (issue #203).

#include <catch2/catch_test_macros.hpp>

#include "beebium/IntegraBBatterySeed.hpp"
#include "beebium/Machines.hpp"
#include "beebium/service/DeviceInspectionService.hpp"

#include <memory>

using namespace beebium;

namespace {

template <typename MachineType>
RealTimeClockState clock_state(MachineType& machine) {
    service::DeviceInspectionServiceImpl<MachineType> inspection(machine);
    RealTimeClockState state;
    REQUIRE(inspection.GetRealTimeClockState(nullptr, nullptr, &state).ok());
    return state;
}

} // namespace

TEST_CASE("The Integra-B's clock is read whole, CMOS RAM included (#203)",
          "[inspection][rtc]") {
    auto machine = std::make_unique<ModelBIntegraB>();
    const auto state = clock_state(*machine);
    CHECK(state.present());
    CHECK(state.chip() == "CDP6818");
    REQUIRE(state.registers().size() == Mc146818Rtc::REGISTER_COUNT);
    for (std::size_t i = 0; i < sizeof(integra_b_battery_seed::CMOS_RAM); ++i) {
        CHECK(static_cast<uint8_t>(state.registers()[Mc146818Rtc::FIRST_RAM_REGISTER + i]) ==
              integra_b_battery_seed::CMOS_RAM[i]);
    }
}

TEST_CASE("Reading the clock moves neither its address latch nor its flags (#203)",
          "[inspection][rtc]") {
    auto machine = std::make_unique<ModelBIntegraB>();
    auto& rtc = machine->state().memory.rtc;
    rtc.write_address(0x1D);
    const uint8_t flags = rtc.peek_register(0x0C);

    const auto state = clock_state(*machine);
    CHECK(state.address_register() == 0x1D);
    CHECK(rtc.address() == 0x1D);
    CHECK(static_cast<uint8_t>(state.registers()[0x0C]) == flags);
    CHECK(rtc.peek_register(0x0C) == flags);
}

TEST_CASE("A machine without a clock of its own reports none (#203)", "[inspection][rtc]") {
    auto machine = std::make_unique<ModelB>();
    const auto state = clock_state(*machine);
    CHECK_FALSE(state.present());
    CHECK(state.registers().empty());
}

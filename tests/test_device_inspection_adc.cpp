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

// DeviceInspection.GetAnalogueConverterState reads the uPD7002 without the
// side effects of the guest's reads: the high byte is not read, so EOC and
// CB1 stay as they are (#98).

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/extension/AnaloguePortDevice.hpp"
#include "beebium/service/DeviceInspectionService.hpp"

#include <memory>

using namespace beebium;

namespace {

template <typename MachineType>
AnalogueConverterState converter_state(MachineType& machine) {
    service::DeviceInspectionServiceImpl<MachineType> inspection(machine);
    AnalogueConverterState state;
    REQUIRE(inspection.GetAnalogueConverterState(nullptr, nullptr, &state).ok());
    return state;
}

template <typename Hardware>
void run_adc_clocks(Hardware& hardware, uint32_t clocks) {
    for (uint32_t i = 0; i < clocks; ++i) {
        (void)hardware.poll_nmi();
    }
}

} // namespace

TEST_CASE("The ADC's power-on state is reported whole (#98)", "[inspection][upd7002]") {
    auto machine = std::make_unique<ModelB>();
    const auto state = converter_state(*machine);
    CHECK(state.present());
    CHECK(state.chip() == "uPD7002");
    CHECK(state.status() == 0xC0);
    CHECK(state.result() == 0);
    CHECK(state.channel() == 0);
    CHECK_FALSE(state.twelve_bit());
    CHECK_FALSE(state.flag());
    CHECK_FALSE(state.busy());
    CHECK(state.eoc_level() == 1);
    CHECK(state.clocks_remaining() == 0);
    REQUIRE(state.input_levels_size() == 4);
    for (int channel = 0; channel < 4; ++channel) {
        CHECK(state.input_levels(channel) == 0xCAFE);
    }
}

TEST_CASE("A conversion in flight is reported with its clocks to go (#98)",
          "[inspection][upd7002]") {
    auto machine = std::make_unique<ModelB>();
    auto& hardware = machine->memory();
    hardware.analogue_converter().set_input_level(2, 0x1230);
    hardware.write(0xFEC0, 0x0E);  // channel 2, 12-bit, flag set
    run_adc_clocks(hardware, 100);

    const auto state = converter_state(*machine);
    CHECK(state.channel() == 2);
    CHECK(state.twelve_bit());
    CHECK(state.flag());
    CHECK(state.busy());
    CHECK(state.eoc_level() == 1);
    CHECK(state.clocks_remaining() ==
          upd7002_timing::conversion_clocks(true, 0x123) - 100);
    CHECK(state.input_levels(2) == 0x1230);
    // Busy with EOC inactive; bits 5 to 0 still the power-on result's.
    CHECK(state.status() == 0x80);
}

TEST_CASE("Reading the ADC leaves EOC and CB1 as they were (#98)", "[inspection][upd7002]") {
    auto machine = std::make_unique<ModelBPlus>();
    auto& hardware = machine->memory();
    hardware.analogue_converter().set_input_level(0, 0xABCD);
    hardware.write(0xFEC0, 0x08);
    run_adc_clocks(hardware, 20000);
    REQUIRE(hardware.analogue_converter().eoc_level() == 0);

    const auto state = converter_state(*machine);
    CHECK(state.result() == 0xABC0);
    CHECK(state.eoc_level() == 0);
    CHECK_FALSE(state.busy());
    CHECK(state.status() == (0x08 | 0x20 | 0x40));
    CHECK(hardware.analogue_converter().eoc_level() == 0);
}

namespace {
struct FixedLevels : AnaloguePortDevice {
    uint16_t channel_level(int channel) override {
        return static_cast<uint16_t>(0x1000 * (channel + 1) + 0x234);
    }
};
}  // namespace

TEST_CASE("Inspection reports a device's levels as the inputs (#98)",
          "[inspection][upd7002][analogue_port]") {
    auto machine = std::make_unique<ModelB>();
    FixedLevels device;
    machine->memory().analogue_port().attach(device);
    const auto state = converter_state(*machine);
    REQUIRE(state.input_levels_size() == 4);
    CHECK(state.input_levels(0) == 0x1234);
    CHECK(state.input_levels(1) == 0x2234);
    CHECK(state.input_levels(2) == 0x3234);
    CHECK(state.input_levels(3) == 0x4234);
}

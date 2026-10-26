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

// The 1 MHz bus's NRST line is asserted on power-up and on BREAK, so every
// machine variant must pass both resets on to the devices on its bus.

#include <beebium/Machines.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace beebium;

namespace {

struct ResetRecordingDevice : OneMHzBusDevice {
    std::vector<ResetCause> resets;

    uint8_t read(uint16_t /*offset*/) override { return 0xFF; }
    void write(uint16_t /*offset*/, uint8_t /*value*/) override {}
    void reset(ResetCause cause) override { resets.push_back(cause); }
};

}  // namespace

TEST_CASE("OneMHzBusPort reset reaches every device once", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    ResetRecordingDevice fred_device;
    ResetRecordingDevice jim_device;
    port.claim_addresses(0x40, 0x43, fred_device);
    port.claim_addresses(0x60, 0x63, fred_device);
    port.claim_jim_pages(0x30, 0x3F, jim_device);
    port.write(0x0FF, 0x3E);

    port.reset(ResetCause::Break);

    REQUIRE(fred_device.resets == std::vector<ResetCause>{ResetCause::Break});
    REQUIRE(jim_device.resets == std::vector<ResetCause>{ResetCause::Break});
    // The paging register is not cleared by reset.
    REQUIRE(port.jim_page() == 0x3E);
}

TEMPLATE_TEST_CASE("Machine resets reach the 1 MHz bus", "[extension][1mhz-bus][reset]",
                   ModelB, ModelBPlus, ModelBPlus128K, ModelBRomRamBoard,
                   ModelBAtplSidewise, ModelBWatfordRomRam, ModelBIntegraB) {
    TestType machine;
    ResetRecordingDevice device;
    machine.memory().one_mhz_bus().claim_jim_pages(0x30, 0x3F, device);

    machine.reset();
    machine.soft_reset();

    REQUIRE(device.resets == std::vector<ResetCause>{ResetCause::PowerOn, ResetCause::Break});
}

// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

// Test compile-time IRQ aggregation system

#include <catch2/catch_test_macros.hpp>
#include <beebium/IrqAggregator.hpp>

using namespace beebium;

namespace {

// Test device that generates IRQs
struct TestIrqDevice {
    bool irq_active = false;

    bool irq_pending() const { return irq_active; }
};

// Another test device
struct AnotherIrqDevice {
    bool irq_flag = false;

    bool irq_pending() const { return irq_flag; }
};

} // anonymous namespace

TEST_CASE("IrqSource concept", "[irq]") {
    STATIC_REQUIRE(IrqSource<TestIrqDevice>);
    STATIC_REQUIRE(IrqSource<AnotherIrqDevice>);
}

TEST_CASE("Single IRQ source", "[irq]") {
    TestIrqDevice device;
    auto aggregator = make_irq_aggregator(make_irq_binding<0>(device));

    // No IRQ pending
    device.irq_active = false;
    CHECK(aggregator.poll() == 0x00);

    // IRQ pending
    device.irq_active = true;
    CHECK(aggregator.poll() == 0x01);
}

TEST_CASE("Multiple IRQ sources", "[irq]") {
    TestIrqDevice device1;
    TestIrqDevice device2;
    AnotherIrqDevice device3;

    auto aggregator = make_irq_aggregator(
        make_irq_binding<0>(device1),
        make_irq_binding<1>(device2),
        make_irq_binding<2>(device3)
    );

    // No IRQs pending
    CHECK(aggregator.poll() == 0x00);

    // Only device1 IRQ
    device1.irq_active = true;
    CHECK(aggregator.poll() == 0x01);

    // Device1 and device2 IRQs
    device2.irq_active = true;
    CHECK(aggregator.poll() == 0x03);

    // All three devices
    device3.irq_flag = true;
    CHECK(aggregator.poll() == 0x07);

    // Clear device1
    device1.irq_active = false;
    CHECK(aggregator.poll() == 0x06);

    // Clear all
    device2.irq_active = false;
    device3.irq_flag = false;
    CHECK(aggregator.poll() == 0x00);
}

TEST_CASE("IRQ bit positions", "[irq]") {
    TestIrqDevice device;

    SECTION("Bit 0") {
        auto agg = make_irq_aggregator(make_irq_binding<0>(device));
        device.irq_active = true;
        CHECK(agg.poll() == 0x01);
    }

    SECTION("Bit 3") {
        auto agg = make_irq_aggregator(make_irq_binding<3>(device));
        device.irq_active = true;
        CHECK(agg.poll() == 0x08);
    }

    SECTION("Bit 7") {
        auto agg = make_irq_aggregator(make_irq_binding<7>(device));
        device.irq_active = true;
        CHECK(agg.poll() == 0x80);
    }
}

TEST_CASE("Non-contiguous bit positions", "[irq]") {
    TestIrqDevice device1;
    TestIrqDevice device2;

    // Bits 0 and 7 (like BBC Micro system VIA and tube)
    auto aggregator = make_irq_aggregator(
        make_irq_binding<0>(device1),
        make_irq_binding<7>(device2)
    );

    device1.irq_active = true;
    CHECK(aggregator.poll() == 0x01);

    device2.irq_active = true;
    CHECK(aggregator.poll() == 0x81);

    device1.irq_active = false;
    CHECK(aggregator.poll() == 0x80);
}

TEST_CASE("Aggregator size", "[irq]") {
    TestIrqDevice d1, d2, d3;

    auto agg1 = make_irq_aggregator(make_irq_binding<0>(d1));
    CHECK(agg1.size() == 1);

    auto agg2 = make_irq_aggregator(
        make_irq_binding<0>(d1),
        make_irq_binding<1>(d2)
    );
    CHECK(agg2.size() == 2);

    auto agg3 = make_irq_aggregator(
        make_irq_binding<0>(d1),
        make_irq_binding<1>(d2),
        make_irq_binding<2>(d3)
    );
    CHECK(agg3.size() == 3);
}

TEST_CASE("Aggregator get() accessor", "[irq]") {
    TestIrqDevice device1;
    TestIrqDevice device2;

    auto aggregator = make_irq_aggregator(
        make_irq_binding<0>(device1),
        make_irq_binding<1>(device2)
    );

    // Access binding by index
    CHECK(aggregator.get<0>().bit == 0);
    CHECK(aggregator.get<1>().bit == 1);

    // Polling through individual binding
    device1.irq_active = true;
    CHECK(aggregator.get<0>().poll() == 0x01);
    CHECK(aggregator.get<1>().poll() == 0x00);
}

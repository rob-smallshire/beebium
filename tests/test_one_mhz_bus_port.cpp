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

#include <beebium/extension/OneMHzBusPort.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace beebium;

namespace {

struct MockDevice : OneMHzBusDevice {
    uint8_t last_read_offset = 0;
    uint8_t last_write_offset = 0;
    uint8_t last_write_value = 0;
    int tick_count = 0;
    uint8_t read_value = 0x42;

    uint8_t read(uint16_t offset) override {
        last_read_offset = static_cast<uint8_t>(offset);
        return read_value;
    }

    void write(uint16_t offset, uint8_t value) override {
        last_write_offset = static_cast<uint8_t>(offset);
        last_write_value = value;
    }

    void tick() override {
        ++tick_count;
    }
};

struct MockJimDevice : MockDevice {
    uint8_t last_jim_read_page = 0;
    uint8_t last_jim_read_offset = 0;
    uint8_t last_jim_write_page = 0;
    uint8_t last_jim_write_offset = 0;
    uint8_t last_jim_write_value = 0;
    int jim_write_count = 0;
    uint8_t jim_read_value = 0x5A;

    uint8_t jim_read(uint8_t page, uint8_t offset) override {
        last_jim_read_page = page;
        last_jim_read_offset = offset;
        return jim_read_value;
    }

    void jim_write(uint8_t page, uint8_t offset, uint8_t value) override {
        last_jim_write_page = page;
        last_jim_write_offset = offset;
        last_jim_write_value = value;
        ++jim_write_count;
    }
};

}  // namespace

TEST_CASE("OneMHzBusPort empty reads return 0xFF", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    REQUIRE(port.read(0x00) == 0xFF);
    REQUIRE(port.read(0x50) == 0xFF);
    REQUIRE(port.read(0xFF) == 0xFF);
    REQUIRE(port.read(0x100) == 0xFF);  // JIM region
    REQUIRE(port.read(0x1FF) == 0xFF);
}

TEST_CASE("OneMHzBusPort empty writes do not crash", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    port.write(0x00, 0x42);
    port.write(0xFF, 0x42);
    port.write(0x1FF, 0x42);
}

TEST_CASE("OneMHzBusPort delegates reads to claimed device", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;
    device.read_value = 0xAB;

    port.claim_addresses(0x50, 0x57, device);

    REQUIRE(port.read(0x50) == 0xAB);
    REQUIRE(device.last_read_offset == 0x50);

    REQUIRE(port.read(0x57) == 0xAB);
    REQUIRE(device.last_read_offset == 0x57);
}

TEST_CASE("OneMHzBusPort delegates writes to claimed device", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    port.claim_addresses(0x50, 0x57, device);

    port.write(0x53, 0xCD);
    REQUIRE(device.last_write_offset == 0x53);
    REQUIRE(device.last_write_value == 0xCD);
}

TEST_CASE("OneMHzBusPort unclaimed addresses unaffected by claims", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    port.claim_addresses(0x50, 0x57, device);

    REQUIRE(port.read(0x4F) == 0xFF);
    REQUIRE(port.read(0x58) == 0xFF);
    REQUIRE(port.read(0x00) == 0xFF);
}

TEST_CASE("OneMHzBusPort rejects overlapping claims", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device1;
    MockDevice device2;

    port.claim_addresses(0x50, 0x57, device1);

    REQUIRE_THROWS_AS(
        port.claim_addresses(0x54, 0x5B, device2),
        std::runtime_error);
}

TEST_CASE("OneMHzBusPort rejects invalid address range", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    REQUIRE_THROWS_AS(
        port.claim_addresses(0x57, 0x50, device),  // reversed
        std::runtime_error);
}

TEST_CASE("OneMHzBusPort tick calls each device once", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device1;
    MockDevice device2;

    port.claim_addresses(0x50, 0x57, device1);
    port.claim_addresses(0x60, 0x67, device2);

    port.tick();

    REQUIRE(device1.tick_count == 1);
    REQUIRE(device2.tick_count == 1);

    port.tick();

    REQUIRE(device1.tick_count == 2);
    REQUIRE(device2.tick_count == 2);
}

TEST_CASE("OneMHzBusPort tick does not double-tick same device", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    // Same device claiming two non-contiguous ranges
    port.claim_addresses(0x50, 0x53, device);
    port.claim_addresses(0x60, 0x63, device);

    port.tick();

    REQUIRE(device.tick_count == 1);
}

TEST_CASE("OneMHzBusPort multiple devices coexist", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device1;
    MockDevice device2;
    device1.read_value = 0x11;
    device2.read_value = 0x22;

    port.claim_addresses(0x40, 0x43, device1);
    port.claim_addresses(0x50, 0x53, device2);

    REQUIRE(port.read(0x40) == 0x11);
    REQUIRE(port.read(0x50) == 0x22);
    REQUIRE(port.read(0x44) == 0xFF);  // gap between devices
}

TEST_CASE("OneMHzBusPort is_claimed query", "[extension][1mhz-bus]") {
    OneMHzBusPort port;
    MockDevice device;

    REQUIRE_FALSE(port.is_claimed(0x50));
    port.claim_addresses(0x50, 0x57, device);
    REQUIRE(port.is_claimed(0x50));
    REQUIRE(port.is_claimed(0x57));
    REQUIRE_FALSE(port.is_claimed(0x58));
}

TEST_CASE("OneMHzBusPort owns the JIM paging register", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;

    SECTION("it starts at 0xFF") {
        REQUIRE(port.jim_page() == 0xFF);
    }

    SECTION("a write to 0xFCFF stores the page") {
        port.write(0x0FF, 0x3E);
        REQUIRE(port.jim_page() == 0x3E);
    }

    SECTION("a read of 0xFCFF floats and leaves the page unchanged") {
        port.write(0x0FF, 0x3E);
        REQUIRE(port.read(0x0FF) == 0xFF);
        REQUIRE(port.jim_page() == 0x3E);
    }

    SECTION("no device may claim it") {
        MockDevice device;
        REQUIRE_THROWS_AS(port.claim_addresses(0xF0, 0xFF, device), std::runtime_error);
        REQUIRE_FALSE(port.is_claimed(0x0FF));
    }
}

TEST_CASE("OneMHzBusPort FRED claims cannot reach JIM", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockDevice device;
    REQUIRE_THROWS_AS(port.claim_addresses(0x100, 0x1FF, device), std::runtime_error);
    REQUIRE_THROWS_AS(port.claim_addresses(0x0FE, 0x100, device), std::runtime_error);
    port.claim_addresses(0x0FE, 0x0FE, device);
    REQUIRE(port.is_claimed(0x0FE));
}

TEST_CASE("OneMHzBusPort routes JIM by the current page", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockJimDevice device;
    port.claim_jim_pages(0x30, 0x3F, device);

    port.write(0x0FF, 0x3E);
    port.write(0x1A5, 0x77);
    REQUIRE(device.jim_write_count == 1);
    REQUIRE(device.last_jim_write_page == 0x3E);
    REQUIRE(device.last_jim_write_offset == 0xA5);
    REQUIRE(device.last_jim_write_value == 0x77);

    device.jim_read_value = 0x9C;
    REQUIRE(port.read(0x100) == 0x9C);
    REQUIRE(device.last_jim_read_page == 0x3E);
    REQUIRE(device.last_jim_read_offset == 0x00);

    // A JIM access never reaches the FRED interface.
    REQUIRE(device.last_write_value == 0);
}

TEST_CASE("OneMHzBusPort unclaimed JIM pages float", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockJimDevice device;
    port.claim_jim_pages(0x30, 0x3F, device);

    port.write(0x0FF, 0x50);
    port.write(0x110, 0x12);
    REQUIRE(device.jim_write_count == 0);
    REQUIRE(port.read(0x110) == 0xFF);

    port.write(0x0FF, 0x2F);
    port.write(0x110, 0x12);
    REQUIRE(device.jim_write_count == 0);
}

TEST_CASE("OneMHzBusPort rejects overlapping and reversed JIM page claims",
          "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockJimDevice device1;
    MockJimDevice device2;
    port.claim_jim_pages(0x30, 0x3F, device1);

    REQUIRE_THROWS_AS(port.claim_jim_pages(0x3F, 0x40, device2), std::runtime_error);
    REQUIRE_THROWS_AS(port.claim_jim_pages(0x20, 0x30, device2), std::runtime_error);
    REQUIRE_THROWS_AS(port.claim_jim_pages(0x60, 0x50, device2), std::runtime_error);
    REQUIRE_FALSE(port.is_jim_page_claimed(0x40));
    REQUIRE_FALSE(port.is_jim_page_claimed(0x2F));
}

TEST_CASE("OneMHzBusPort two JIM devices on disjoint page ranges", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockJimDevice music5000;
    MockJimDevice music3000;
    port.claim_jim_pages(0x30, 0x3F, music5000);
    port.claim_jim_pages(0x50, 0x5F, music3000);

    port.write(0x0FF, 0x31);
    port.write(0x101, 0xA1);
    port.write(0x0FF, 0x5F);
    port.write(0x102, 0xB2);

    REQUIRE(music5000.jim_write_count == 1);
    REQUIRE(music5000.last_jim_write_page == 0x31);
    REQUIRE(music5000.last_jim_write_value == 0xA1);
    REQUIRE(music3000.jim_write_count == 1);
    REQUIRE(music3000.last_jim_write_page == 0x5F);
    REQUIRE(music3000.last_jim_write_value == 0xB2);
}

TEST_CASE("OneMHzBusPort FRED and JIM devices coexist", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockDevice scsi;
    MockJimDevice synth;
    scsi.read_value = 0x11;
    port.claim_addresses(0x40, 0x43, scsi);
    port.claim_jim_pages(0x30, 0x3F, synth);

    port.write(0x041, 0x22);
    port.write(0x0FF, 0x30);
    port.write(0x180, 0x33);

    REQUIRE(scsi.last_write_offset == 0x41);
    REQUIRE(scsi.last_write_value == 0x22);
    REQUIRE(port.read(0x040) == 0x11);
    REQUIRE(synth.jim_write_count == 1);
    REQUIRE(synth.last_jim_write_offset == 0x80);

    port.tick();
    REQUIRE(scsi.tick_count == 1);
    REQUIRE(synth.tick_count == 1);
}

TEST_CASE("OneMHzBusPort ticks a FRED-and-JIM device once", "[extension][1mhz-bus][jim]") {
    OneMHzBusPort port;
    MockJimDevice device;
    port.claim_addresses(0x60, 0x63, device);
    port.claim_jim_pages(0x30, 0x3F, device);

    port.tick();
    REQUIRE(device.tick_count == 1);
}

TEST_CASE("OneMHzBusDevice JIM defaults float and ignore writes", "[extension][1mhz-bus][jim]") {
    MockDevice device;
    OneMHzBusDevice& base = device;
    REQUIRE(base.jim_read(0x30, 0x00) == 0xFF);
    base.jim_write(0x30, 0x00, 0x42);
    REQUIRE(device.last_write_value == 0);
}

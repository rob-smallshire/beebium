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

// Unit tests for the Piconet device-discovery decision (VID filtering plus
// the one/zero/many outcome). Server-free: the enumerator and prober are
// injected, so no serial hardware or gRPC is involved.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "beebium/econet/piconet/Constants.hpp"
#include "beebium/econet/piconet/Discovery.hpp"
#include "beebium/serial/EnumeratePorts.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using beebium::piconet::discover_piconet_device;
using beebium::serial::SerialPortInfo;
using Catch::Matchers::ContainsSubstring;

namespace {

SerialPortInfo pico_port(const std::string& path,
                         const std::string& serial = "SN") {
    SerialPortInfo info;
    info.path = path;
    info.usb_vendor_id = beebium::piconet::USB_VENDOR_ID;   // 0x2E8A
    info.usb_product_id = beebium::piconet::USB_PRODUCT_ID;  // 0x000A
    info.serial_number = serial;
    info.product = "Pico";
    info.manufacturer = "Raspberry Pi";
    return info;
}

SerialPortInfo other_usb_port(const std::string& path, std::uint16_t vid) {
    SerialPortInfo info;
    info.path = path;
    info.usb_vendor_id = vid;
    info.usb_product_id = 0x1234;
    return info;
}

SerialPortInfo non_usb_port(const std::string& path) {
    SerialPortInfo info;
    info.path = path;  // no USB identity at all
    return info;
}

// A prober that confirms a fixed set of paths.
beebium::piconet::PortProber confirming(std::vector<std::string> ok) {
    return [ok = std::move(ok)](const std::string& path) {
        return std::find(ok.begin(), ok.end(), path) != ok.end();
    };
}

}  // namespace

TEST_CASE("discovery honours an explicit device_path without probing",
          "[piconet][discovery]") {
    bool enumerated = false;
    bool probed = false;
    auto result = discover_piconet_device(
        "/dev/cu.usbmodem101",
        [&] { enumerated = true; return std::vector<SerialPortInfo>{}; },
        [&](const std::string&) { probed = true; return false; });

    CHECK(result.ok);
    CHECK(result.device_path == "/dev/cu.usbmodem101");
    CHECK_FALSE(enumerated);  // an explicit path short-circuits discovery
    CHECK_FALSE(probed);
}

TEST_CASE("discovery treats \"auto\" like an absent path", "[piconet][discovery]") {
    auto ports = std::vector<SerialPortInfo>{pico_port("/dev/cu.usbmodem101")};
    auto result = discover_piconet_device(
        "auto",
        [&] { return ports; },
        confirming({"/dev/cu.usbmodem101"}));

    CHECK(result.ok);
    CHECK(result.device_path == "/dev/cu.usbmodem101");
}

TEST_CASE("discovery selects the unique confirmed Pico-VID port",
          "[piconet][discovery]") {
    std::vector<SerialPortInfo> ports{
        non_usb_port("/dev/cu.Bluetooth-Incoming-Port"),
        other_usb_port("/dev/cu.usbserial-FTDI", 0x0403),
        pico_port("/dev/cu.usbmodem101"),
    };

    std::vector<std::string> probed;
    auto result = discover_piconet_device(
        "",
        [&] { return ports; },
        [&](const std::string& path) {
            probed.push_back(path);
            return path == "/dev/cu.usbmodem101";
        });

    CHECK(result.ok);
    CHECK(result.device_path == "/dev/cu.usbmodem101");
    // Only the Pico-VID port is probed: the non-USB and FTDI ports never are.
    REQUIRE(probed.size() == 1);
    CHECK(probed[0] == "/dev/cu.usbmodem101");
}

TEST_CASE("discovery fails clearly when no Pico-VID port is present",
          "[piconet][discovery]") {
    std::vector<SerialPortInfo> ports{
        non_usb_port("/dev/cu.Bluetooth-Incoming-Port"),
        other_usb_port("/dev/cu.usbserial-FTDI", 0x0403),
    };
    auto result = discover_piconet_device(
        "", [&] { return ports; }, confirming({}));

    CHECK_FALSE(result.ok);
    CHECK(result.device_path.empty());
    CHECK_THAT(result.message, ContainsSubstring("0x2E8A"));
    CHECK_THAT(result.message, ContainsSubstring("device_path"));
    // Names how many ports were searched.
    CHECK_THAT(result.message, ContainsSubstring("2"));
}

TEST_CASE("discovery fails when a Pico-VID port does not answer the probe",
          "[piconet][discovery]") {
    std::vector<SerialPortInfo> ports{pico_port("/dev/cu.usbmodem101")};
    auto result = discover_piconet_device(
        "", [&] { return ports; }, confirming({}));  // VID matches, probe fails

    CHECK_FALSE(result.ok);
    CHECK_THAT(result.message, ContainsSubstring("/dev/cu.usbmodem101"));
    CHECK_THAT(result.message, ContainsSubstring("STATUS probe"));
    CHECK_THAT(result.message, ContainsSubstring("device_path"));
}

TEST_CASE("discovery refuses to guess when several Piconets are confirmed",
          "[piconet][discovery]") {
    std::vector<SerialPortInfo> ports{
        pico_port("/dev/cu.usbmodem101", "AAAA"),
        pico_port("/dev/cu.usbmodem201", "BBBB"),
    };
    auto result = discover_piconet_device(
        "", [&] { return ports; },
        confirming({"/dev/cu.usbmodem101", "/dev/cu.usbmodem201"}));

    CHECK_FALSE(result.ok);
    CHECK_THAT(result.message, ContainsSubstring("Multiple"));
    CHECK_THAT(result.message, ContainsSubstring("/dev/cu.usbmodem101"));
    CHECK_THAT(result.message, ContainsSubstring("/dev/cu.usbmodem201"));
}

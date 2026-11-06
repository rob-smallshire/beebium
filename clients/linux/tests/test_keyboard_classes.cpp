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

// The attached keyboard classes (docs/keyboard-mapping-files.md 3.5), read
// from the host's input devices as /proc/bus/input/devices lists them.

#include "keyboard/KeyboardClasses.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

// A device block as the kernel writes it. `letters` gives it the letter keys
// a typing keyboard has.
std::string block(const char* vendor, const char* product, const char* name,
                  const char* handlers, bool letters) {
    std::string text;
    text += std::string("I: Bus=0003 Vendor=") + vendor + " Product=" + product +
            " Version=0111\n";
    text += std::string("N: Name=\"") + name + "\"\n";
    text += "P: Phys=usb-0000:01:00.0-1.4/input0\n";
    text += "S: Sysfs=/devices/platform/scb/fd500000.pcie/input/input0\n";
    text += "U: Uniq=\n";
    text += std::string("H: Handlers=") + handlers + "\n";
    text += "B: PROP=0\n";
    text += "B: EV=120013\n";
    text += letters ? "B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n"
                    : "B: KEY=c000 10000000000000 0\n";
    text += "B: MSC=10\n";
    text += "\n";
    return text;
}

}  // namespace

TEST_CASE("The Pi 400's own keyboard is the Pi internal class", "[keyboard][classes]") {
    const std::string devices =
        block("04d9", "0007", "Raspberry Pi Internal Keyboard", "sysrq kbd leds event0", true) +
        block("04d9", "0007", "Raspberry Pi Internal Keyboard Consumer Control",
              "kbd event1", false) +
        block("0000", "0000", "vc4-hdmi-0", "kbd event2", false);
    const auto keyboards = keyboards_in_input_devices(devices);
    REQUIRE(keyboards.size() == 1);
    CHECK(keyboards[0].name == "Raspberry Pi Internal Keyboard");
    CHECK(keyboards[0].vendor == 0x04d9);
    CHECK(keyboards[0].product == 0x0007);
    CHECK(keyboards[0].keyboard_class == "pi-internal");
    CHECK(keyboard_classes(keyboards) == KeyboardClasses{"pi-internal"});
}

TEST_CASE("A keyboard is Pi internal by its name or by the Pi 400's ids", "[keyboard][classes]") {
    // A Pi 500, say, under ids not yet seen.
    CHECK(keyboard_class_of("Raspberry Pi 500 Internal Keyboard", 0x2e8a, 0x0010) ==
          "pi-internal");
    CHECK(keyboard_class_of("raspberry pi internal keyboard", 0x1234, 0x5678) == "pi-internal");
    // The Pi 400's ids under another name.
    CHECK(keyboard_class_of("USB Keyboard", 0x04d9, 0x0007) == "pi-internal");
    // The external official keyboard has a real F12: standard.
    CHECK(keyboard_class_of("Raspberry Pi Keyboard", 0x04d9, 0x0006) == "standard");
    CHECK(keyboard_class_of("Logitech USB Keyboard", 0x046d, 0xc31c) == "standard");
}

TEST_CASE("Both classes are attached when a Pi 400 has an external keyboard too",
          "[keyboard][classes]") {
    const std::string devices =
        block("04d9", "0007", "Raspberry Pi Internal Keyboard", "sysrq kbd leds event0", true) +
        block("046d", "c31c", "Logitech USB Keyboard", "sysrq kbd leds event3", true);
    CHECK(keyboard_classes(keyboards_in_input_devices(devices)) ==
          KeyboardClasses{"pi-internal", "standard"});
}

TEST_CASE("Only typing keyboards count: buttons and remote controls do not",
          "[keyboard][classes]") {
    const std::string devices =
        block("0000", "0000", "pwr_button", "kbd event0", false) +
        block("0000", "0000", "vc4-hdmi-1", "kbd event1", false) +
        block("046d", "c52b", "Logitech USB Receiver Mouse", "mouse0 event2", true);
    CHECK(keyboards_in_input_devices(devices).empty());
    // With no keyboard to read, a standard one is taken to be there.
    CHECK(keyboard_classes({}) == KeyboardClasses{"standard"});
    CHECK(keyboard_classes(keyboards_in_input_devices("")) == KeyboardClasses{"standard"});
}

TEST_CASE("The startup line names every keyboard with its class and ids", "[keyboard][classes]") {
    const std::string devices =
        block("04d9", "0007", "Raspberry Pi Internal Keyboard", "sysrq kbd leds event0", true) +
        block("046d", "c31c", "Logitech USB Keyboard", "sysrq kbd leds event3", true);
    CHECK(keyboards_description(keyboards_in_input_devices(devices)) ==
          "pi-internal \"Raspberry Pi Internal Keyboard\" (04d9:0007), "
          "standard \"Logitech USB Keyboard\" (046d:c31c)");
    CHECK(keyboards_description({}) == "none found, taken as standard");
}

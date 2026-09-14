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

// Tests exercise the POSIX test seam (enumerate_ports_from_dirs) against
// a tmpfs fixture. The Windows enumerator relies on QueryDosDeviceW and
// does not have an equivalent injection seam; it is covered only by the
// manual Slioch run documented in the plan. All tests here are
// POSIX-only and wrapped in #ifndef _WIN32.

#ifndef _WIN32

#include "beebium/serial/EnumeratePorts.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

// RAII fixture: creates a scratch directory under the platform temp
// directory and removes it on destruction. Inside, create files or
// sub-directories representing the platform's /dev and
// /dev/serial/by-id contents.
struct TmpDir {
    fs::path root;

    TmpDir() {
        auto base = fs::temp_directory_path() /
            ("beebium_enumerate_ports_" + std::to_string(::getpid()));
        // Ensure a fresh dir for each test; remove any leftover from a
        // previous crashed run before creating.
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(base);
        root = base;
    }

    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path subdir(const std::string& name) {
        auto p = root / name;
        fs::create_directories(p);
        return p;
    }

    void touch(const fs::path& path) {
        std::ofstream(path).close();
    }
};

}  // namespace

TEST_CASE("enumerate_ports_from_dirs returns empty when dev_dir missing",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto result = beebium::serial::enumerate_ports_from_dirs(
        (tmp.root / "nonexistent").string(), "");
    CHECK(result.empty());
}

TEST_CASE("enumerate_ports_from_dirs filters by the platform's tty prefixes",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto dev = tmp.subdir("dev");

    // Files that should match on at least one platform.
#ifdef __APPLE__
    tmp.touch(dev / "tty.usbmodem101");
    tmp.touch(dev / "tty.usbserial-A1B2");
#else
    tmp.touch(dev / "ttyUSB0");
    tmp.touch(dev / "ttyACM0");
#endif

    // Files that should never match on any platform.
    tmp.touch(dev / "random_device");
    tmp.touch(dev / "null");
    tmp.touch(dev / "zero");

    auto result = beebium::serial::enumerate_ports_from_dirs(dev.string(), "");
    REQUIRE(result.size() == 2);

#ifdef __APPLE__
    CHECK(result[0] == (dev / "tty.usbmodem101").string());
    CHECK(result[1] == (dev / "tty.usbserial-A1B2").string());
#else
    CHECK(result[0] == (dev / "ttyACM0").string());
    CHECK(result[1] == (dev / "ttyUSB0").string());
#endif
}

#ifdef __APPLE__
TEST_CASE("enumerate_ports_from_dirs includes macOS call-out (cu.*) ports",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto dev = tmp.subdir("dev");

    tmp.touch(dev / "cu.usbserial-A1B2");
    tmp.touch(dev / "cu.usbmodem401");
    // A socat virtual port named with a cu.usbserial- prefix is discoverable too.
    tmp.touch(dev / "cu.usbserial-beeb");
    // A non-USB call-out device is not matched (kept focused on USB serial).
    tmp.touch(dev / "cu.Bluetooth-Incoming-Port");

    auto result = beebium::serial::enumerate_ports_from_dirs(dev.string(), "");
    REQUIRE(result.size() == 3);  // the three cu.usb* entries, sorted
    CHECK(result[0] == (dev / "cu.usbmodem401").string());
    CHECK(result[1] == (dev / "cu.usbserial-A1B2").string());
    CHECK(result[2] == (dev / "cu.usbserial-beeb").string());
}
#endif

TEST_CASE("enumerate_ports_from_dirs results are sorted lexicographically",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto dev = tmp.subdir("dev");
#ifdef __APPLE__
    tmp.touch(dev / "tty.usbmodem201");
    tmp.touch(dev / "tty.usbmodem101");
    tmp.touch(dev / "tty.usbmodem301");
#else
    tmp.touch(dev / "ttyUSB2");
    tmp.touch(dev / "ttyUSB0");
    tmp.touch(dev / "ttyUSB1");
#endif

    auto result = beebium::serial::enumerate_ports_from_dirs(dev.string(), "");
    REQUIRE(result.size() == 3);
    CHECK(std::is_sorted(result.begin(), result.end()));
}

TEST_CASE("enumerate_ports_from_dirs includes by-id entries verbatim",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto dev = tmp.subdir("dev");
    auto by_id = tmp.subdir("by-id");

#ifdef __APPLE__
    tmp.touch(dev / "tty.usbmodem101");
#else
    tmp.touch(dev / "ttyACM0");
#endif
    tmp.touch(by_id / "usb-Raspberry_Pi_Pico_E660C0D1F3-if00");
    tmp.touch(by_id / "usb-Acorn_Econet_Adapter-if00");

    auto result = beebium::serial::enumerate_ports_from_dirs(
        dev.string(), by_id.string());

    REQUIRE(result.size() == 3);

    // The by-id entries should appear in the result with the by_id_dir
    // prefix preserved (not resolved to the dev_dir target).
    auto has_entry = [&](const std::string& relative) {
        return std::find(result.begin(), result.end(),
                         (by_id / relative).string()) != result.end();
    };
    CHECK(has_entry("usb-Raspberry_Pi_Pico_E660C0D1F3-if00"));
    CHECK(has_entry("usb-Acorn_Econet_Adapter-if00"));
}

TEST_CASE("enumerate_ports_from_dirs skips '.' and '..' in by-id",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto by_id = tmp.subdir("by-id");
    tmp.touch(by_id / "usb-Real_Device-if00");

    // The directory entries . and .. are present in every dir; make
    // sure the enumerator doesn't leak them into the port list.
    auto result = beebium::serial::enumerate_ports_from_dirs(
        (tmp.root / "no-dev").string(), by_id.string());
    REQUIRE(result.size() == 1);
    CHECK(result[0] == (by_id / "usb-Real_Device-if00").string());
}

TEST_CASE("enumerate_ports_from_dirs deduplicates identical paths",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto shared = tmp.subdir("shared");
#ifdef __APPLE__
    tmp.touch(shared / "tty.usbmodem101");
#else
    tmp.touch(shared / "ttyACM0");
#endif
    // Pass the same directory as both dev and by_id; without dedup the
    // by-id scan would surface the same matching file a second time.
    auto result = beebium::serial::enumerate_ports_from_dirs(
        shared.string(), shared.string());
    REQUIRE(result.size() == 1);
}

// --- USB-identity enumeration (enumerate_serial_ports) ---

TEST_CASE("enumerate_serial_ports runs and yields well-formed entries",
          "[serial][enumerate]") {
    // Hardware-agnostic smoke test: whatever is (or isn't) plugged in, the
    // enumerator must not crash and every entry must carry a non-empty path.
    auto ports = beebium::serial::enumerate_serial_ports();
    for (const auto& port : ports) {
        CHECK_FALSE(port.path.empty());
    }
    // Paths are sorted and unique.
    for (std::size_t i = 1; i < ports.size(); ++i) {
        CHECK(ports[i - 1].path < ports[i].path);
    }
}

#ifdef __linux__
namespace {

// Write a sysfs-style attribute file (value + trailing newline, as the
// kernel presents them).
void write_attr(const fs::path& filepath, const std::string& value) {
    std::ofstream out(filepath);
    out << value << "\n";
}

}  // namespace

TEST_CASE("enumerate_serial_ports_from_sysfs reads USB identity via the "
          "interface's parent device",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto sys_tty = tmp.subdir("sys/class/tty");

    // Real topology: the tty's "device" is the USB interface (1-1:1.0),
    // whose PARENT (1-1) is the USB device carrying idVendor/idProduct and
    // the string descriptors.
    auto usb_device = tmp.subdir("sys/devices/usb1/1-1");
    auto usb_iface = tmp.subdir("sys/devices/usb1/1-1/1-1:1.0");
    write_attr(usb_device / "idVendor", "2e8a");
    write_attr(usb_device / "idProduct", "000a");
    write_attr(usb_device / "serial", "E660C0D1F3");
    write_attr(usb_device / "product", "Pico");
    write_attr(usb_device / "manufacturer", "Raspberry Pi");

    auto tty = tmp.subdir("sys/class/tty/ttyACM0");
    fs::create_directory_symlink(usb_iface, tty / "device");

    auto ports = beebium::serial::enumerate_serial_ports_from_sysfs(
        sys_tty.string(), "/dev");

    REQUIRE(ports.size() == 1);
    CHECK(ports[0].path == "/dev/ttyACM0");
    REQUIRE(ports[0].usb_vendor_id.has_value());
    CHECK(*ports[0].usb_vendor_id == 0x2e8a);
    REQUIRE(ports[0].usb_product_id.has_value());
    CHECK(*ports[0].usb_product_id == 0x000a);
    CHECK(ports[0].serial_number == "E660C0D1F3");
    CHECK(ports[0].product == "Pico");
    CHECK(ports[0].manufacturer == "Raspberry Pi");
}

TEST_CASE("enumerate_serial_ports_from_sysfs ignores non-serial ttys",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto sys_tty = tmp.subdir("sys/class/tty");
    // A console tty with no matching prefix must be skipped even if it has
    // a device link.
    auto console = tmp.subdir("sys/class/tty/tty0");
    auto platform_dev = tmp.subdir("sys/devices/platform/serial8250");
    fs::create_directory_symlink(platform_dev, console / "device");

    auto ports = beebium::serial::enumerate_serial_ports_from_sysfs(
        sys_tty.string(), "/dev");
    CHECK(ports.empty());
}

TEST_CASE("enumerate_serial_ports_from_sysfs tolerates a serial tty with no "
          "USB ancestor",
          "[serial][enumerate]") {
    TmpDir tmp;
    auto sys_tty = tmp.subdir("sys/class/tty");
    auto tty = tmp.subdir("sys/class/tty/ttyUSB0");
    auto non_usb = tmp.subdir("sys/devices/platform/some-uart");
    fs::create_directory_symlink(non_usb, tty / "device");

    auto ports = beebium::serial::enumerate_serial_ports_from_sysfs(
        sys_tty.string(), "/dev");
    // Still enumerated (a caller filtering by VID simply won't match it),
    // but with no USB identity.
    REQUIRE(ports.size() == 1);
    CHECK(ports[0].path == "/dev/ttyUSB0");
    CHECK_FALSE(ports[0].usb_vendor_id.has_value());
}
#endif  // __linux__

// A hidden, hardware-dependent probe of the real enumerator. Selected
// explicitly with the [.piconet-live] tag; skipped in the normal run.
TEST_CASE("enumerate_serial_ports sees a live Raspberry Pi Pico VID",
          "[.piconet-live]") {
    auto ports = beebium::serial::enumerate_serial_ports();
    bool saw_pico = false;
    for (const auto& port : ports) {
        if (port.usb_vendor_id && *port.usb_vendor_id == 0x2E8A) {
            saw_pico = true;
        }
    }
    CHECK(saw_pico);
}

#endif  // !_WIN32

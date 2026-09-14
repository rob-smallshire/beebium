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

#ifndef _WIN32

#include "beebium/serial/EnumeratePorts.hpp"

#include <algorithm>
#include <dirent.h>
#include <string>
#include <string_view>
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <cstring>
#elif defined(__linux__)
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#endif

namespace beebium::serial {

namespace {

bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), s.begin());
}

// Tests name against the platform's tty-prefix set. macOS and Linux use
// different conventions; the caller decides which set to apply. Kept as
// a single overload rather than one-per-platform because both sets are
// tiny (two entries) and inlining the test avoids a per-file split.
bool name_is_serial_tty(std::string_view name) noexcept {
#ifdef __APPLE__
    // The call-out (cu.*) nodes are the ones you open for a serial bridge; the
    // dial-in (tty.*) forms are listed too for completeness. A virtual port made
    // with `socat pty,link=/dev/cu.usbserial-<name>` therefore also shows up.
    return starts_with(name, "cu.usbmodem") ||
           starts_with(name, "cu.usbserial") ||
           starts_with(name, "tty.usbmodem") ||
           starts_with(name, "tty.usbserial");
#else
    return starts_with(name, "ttyUSB") ||
           starts_with(name, "ttyACM");
#endif
}

}  // namespace

std::vector<std::string> enumerate_ports_from_dirs(
    const std::string& dev_dir,
    const std::string& by_id_dir)
{
    std::vector<std::string> ports;

    // Scan dev_dir for prefix-matched tty devices. opendir failure is
    // silently tolerated (the directory may not exist in test fixtures
    // or on stripped-down systems); callers get an empty result rather
    // than an error.
    if (DIR* d = ::opendir(dev_dir.c_str())) {
        while (struct dirent* entry = ::readdir(d)) {
            std::string_view name(entry->d_name);
            if (name_is_serial_tty(name)) {
                ports.push_back(dev_dir + "/" + std::string(name));
            }
        }
        ::closedir(d);
    }

    // On Linux the kernel exposes stable symlink names under
    // /dev/serial/by-id/usb-<vendor>_<product>-... which persist across
    // reboots and USB renumbering. When present, surface them verbatim
    // so the user can pick a stable identifier and not worry about
    // ttyACM<N> renumbering after a reconnect. Skipped on macOS
    // (by_id_dir passed empty by the default enumerate_ports).
    if (!by_id_dir.empty()) {
        if (DIR* d = ::opendir(by_id_dir.c_str())) {
            while (struct dirent* entry = ::readdir(d)) {
                std::string_view name(entry->d_name);
                if (name == "." || name == "..") continue;
                ports.push_back(by_id_dir + "/" + std::string(name));
            }
            ::closedir(d);
        }
    }

    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

namespace {

// Sort by path and drop duplicate paths, keeping the first (richest, since
// the platform code fills identity before pushing). Shared by both
// enumerate_ports() and the info variant.
void sort_and_dedup(std::vector<SerialPortInfo>& infos) {
    std::sort(infos.begin(), infos.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) {
                  return a.path < b.path;
              });
    infos.erase(std::unique(infos.begin(), infos.end(),
                            [](const SerialPortInfo& a, const SerialPortInfo& b) {
                                return a.path == b.path;
                            }),
                infos.end());
}

#ifdef __APPLE__

// Copy a CFStringRef into a std::string (UTF-8). Empty on null/failure.
std::string cf_string_to_std(CFStringRef s) {
    if (!s) return {};
    CFIndex length = CFStringGetLength(s);
    CFIndex capacity =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(capacity), '\0');
    if (!CFStringGetCString(s, out.data(), capacity, kCFStringEncodingUTF8)) {
        return {};
    }
    out.resize(std::strlen(out.c_str()));
    return out;
}

// Search a serial service and its USB-device ancestors for a CFString
// property, returning it as std::string. Empty if absent.
std::string search_string_property(io_object_t service, CFStringRef key) {
    auto ref = static_cast<CFStringRef>(IORegistryEntrySearchCFProperty(
        service, kIOServicePlane, key, kCFAllocatorDefault,
        kIORegistryIterateRecursively | kIORegistryIterateParents));
    if (!ref) return {};
    std::string out;
    if (CFGetTypeID(ref) == CFStringGetTypeID()) {
        out = cf_string_to_std(ref);
    }
    CFRelease(ref);
    return out;
}

// Search for a 16-bit integer USB property (idVendor / idProduct).
std::optional<std::uint16_t> search_u16_property(io_object_t service,
                                                 CFStringRef key) {
    auto ref = static_cast<CFNumberRef>(IORegistryEntrySearchCFProperty(
        service, kIOServicePlane, key, kCFAllocatorDefault,
        kIORegistryIterateRecursively | kIORegistryIterateParents));
    if (!ref) return std::nullopt;
    std::optional<std::uint16_t> out;
    if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
        int value = 0;
        if (CFNumberGetValue(ref, kCFNumberIntType, &value)) {
            out = static_cast<std::uint16_t>(value & 0xFFFF);
        }
    }
    CFRelease(ref);
    return out;
}

std::vector<SerialPortInfo> enumerate_serial_ports_apple() {
    std::vector<SerialPortInfo> infos;

    CFMutableDictionaryRef matching = IOServiceMatching(kIOSerialBSDServiceValue);
    if (!matching) return infos;

    io_iterator_t iterator = IO_OBJECT_NULL;
    // IOServiceGetMatchingServices consumes a reference on `matching`.
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) !=
        KERN_SUCCESS) {
        return infos;
    }

    for (io_object_t service = IOIteratorNext(iterator); service;
         service = IOIteratorNext(iterator)) {
        // The call-out node (/dev/cu.*) is the one a bridge opens.
        std::string path;
        if (auto callout = static_cast<CFStringRef>(IORegistryEntryCreateCFProperty(
                service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0))) {
            if (CFGetTypeID(callout) == CFStringGetTypeID()) {
                path = cf_string_to_std(callout);
            }
            CFRelease(callout);
        }
        if (!path.empty()) {
            SerialPortInfo info;
            info.path = std::move(path);
            info.usb_vendor_id = search_u16_property(service, CFSTR("idVendor"));
            info.usb_product_id = search_u16_property(service, CFSTR("idProduct"));
            info.serial_number = search_string_property(service, CFSTR("USB Serial Number"));
            info.product = search_string_property(service, CFSTR("USB Product Name"));
            info.manufacturer = search_string_property(service, CFSTR("USB Vendor Name"));
            infos.push_back(std::move(info));
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);

    sort_and_dedup(infos);
    return infos;
}

#elif defined(__linux__)

// Read a whole sysfs attribute file, trimming a trailing newline. Empty on
// any failure (missing file, unreadable).
std::string read_sysfs_attr(const std::filesystem::path& filepath) {
    std::ifstream in(filepath);
    if (!in) return {};
    std::string value;
    std::getline(in, value);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::optional<std::uint16_t> parse_hex_u16(const std::string& text) {
    if (text.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        unsigned long value = std::stoul(text, &consumed, 16);
        if (consumed != text.size() || value > 0xFFFF) return std::nullopt;
        return static_cast<std::uint16_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

#endif

}  // namespace

#ifdef __linux__

std::vector<SerialPortInfo> enumerate_serial_ports_from_sysfs(
    const std::string& sysfs_class_tty_dir,
    const std::string& dev_dir) {
    namespace fs = std::filesystem;
    std::vector<SerialPortInfo> infos;

    std::error_code ec;
    fs::directory_iterator it(sysfs_class_tty_dir, ec);
    if (ec) return infos;

    for (const auto& entry : it) {
        const std::string name = entry.path().filename().string();
        if (!name_is_serial_tty(name)) continue;

        // The tty's "device" symlink points at the owning device node. The
        // USB device (carrying idVendor) is the nearest ancestor of that
        // node that has an idVendor attribute -- the intervening node is
        // typically the USB interface (e.g. 3-1:1.0 under 3-1).
        fs::path device_link = entry.path() / "device";
        fs::path device = fs::canonical(device_link, ec);
        if (ec) {
            ec.clear();
            continue;  // No resolvable device: not a USB serial port.
        }

        SerialPortInfo info;
        info.path = dev_dir + "/" + name;

        // Walk up at most a few levels looking for idVendor.
        for (int level = 0; level < 6 && !device.empty(); ++level) {
            if (fs::exists(device / "idVendor", ec)) {
                info.usb_vendor_id = parse_hex_u16(read_sysfs_attr(device / "idVendor"));
                info.usb_product_id = parse_hex_u16(read_sysfs_attr(device / "idProduct"));
                info.serial_number = read_sysfs_attr(device / "serial");
                info.product = read_sysfs_attr(device / "product");
                info.manufacturer = read_sysfs_attr(device / "manufacturer");
                break;
            }
            fs::path parent = device.parent_path();
            if (parent == device) break;
            device = parent;
        }

        infos.push_back(std::move(info));
    }

    sort_and_dedup(infos);
    return infos;
}

#endif  // __linux__

std::vector<SerialPortInfo> enumerate_serial_ports() {
#ifdef __APPLE__
    return enumerate_serial_ports_apple();
#elif defined(__linux__)
    return enumerate_serial_ports_from_sysfs("/sys/class/tty", "/dev");
#else
    return {};
#endif
}

std::vector<std::string> enumerate_ports() {
#ifdef __APPLE__
    // macOS has no kernel-level stable-id scheme comparable to Linux's
    // /dev/serial/by-id. Device nodes are assigned in USB enumeration
    // order, which can change across reconnects (this is exactly the
    // renumbering problem the ModalEditor-based device-path editor
    // exists to let the user work around).
    return enumerate_ports_from_dirs("/dev", "");
#else
    // Linux: the by-id directory is only present if the kernel has the
    // usb-serial scheme built in; if absent enumerate_ports_from_dirs
    // silently skips it.
    return enumerate_ports_from_dirs("/dev", "/dev/serial/by-id");
#endif
}

}  // namespace beebium::serial

#endif  // !_WIN32

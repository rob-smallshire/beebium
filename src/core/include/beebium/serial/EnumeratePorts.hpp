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

#ifndef BEEBIUM_SERIAL_ENUMERATE_PORTS_HPP
#define BEEBIUM_SERIAL_ENUMERATE_PORTS_HPP

// Host serial-port enumeration for populating UI pickers (e.g. the
// ModalEditor inside the Piconet extension's panel).
//
// This is a best-effort, UI-oriented helper:
//   * macOS:   /dev/cu.usbmodem*, /dev/cu.usbserial* (call-out, preferred) plus
//              the /dev/tty.usbmodem*, /dev/tty.usbserial* dial-in forms
//   * Linux:   /dev/ttyUSB*, /dev/ttyACM*, plus any symlink entries
//              in /dev/serial/by-id/ (the stable-id form preferred
//              where the kernel provides it)
//   * Windows: QueryDosDeviceW-reported COM<n> names
//
// Not a source-of-truth; callers should still validate / try-open the
// path returned. A missing / unreadable enumeration target yields an
// empty result rather than an error. Entries are sorted lexicographically
// and deduplicated.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beebium::serial {

// Enumerate host serial ports. Best-effort; returns an empty vector on
// platforms or configurations where enumeration isn't possible.
std::vector<std::string> enumerate_ports();

// One enumerated serial port together with whatever USB identity the OS
// can associate with it. The USB fields are best-effort: a non-USB port
// (or a platform that cannot report the identity) leaves the ids empty and
// the strings blank. Consumers that need to positively select a device
// (e.g. filtering to the Raspberry Pi Pico's VID 0x2E8A before probing for
// a Piconet) match on usb_vendor_id / usb_product_id where present.
struct SerialPortInfo {
    std::string path;                            // OS device path (macOS callout, Linux /dev, Windows COMn)
    std::optional<std::uint16_t> usb_vendor_id;  // USB idVendor, if the port is a USB device
    std::optional<std::uint16_t> usb_product_id; // USB idProduct, if known
    std::string serial_number;                   // USB iSerial string, if any
    std::string product;                         // USB iProduct string, if any
    std::string manufacturer;                    // USB iManufacturer string, if any
};

// Enumerate host serial ports with their USB identity. Best-effort: returns
// an empty vector where enumeration isn't possible, and populates the USB
// fields only where the OS exposes them. Entries are sorted by path and
// deduplicated.
//
//   * macOS:   IOKit IOSerialBSDClient services; the callout (/dev/cu.*)
//              path plus idVendor/idProduct and the USB string descriptors
//              found by searching the parent USB device.
//   * Linux:   /sys/class/tty/<name> whose device resolves under a USB
//              interface; idVendor/idProduct/serial/product/manufacturer
//              read from the owning USB device directory.
//   * Windows: SetupAPI COM-port devices; VID/PID parsed from the hardware
//              id, plus the friendly name.
std::vector<SerialPortInfo> enumerate_serial_ports();

#ifndef _WIN32
#ifdef __linux__
// Test seam (Linux): enumerate from an arbitrary /sys/class/tty-shaped
// directory tree rather than the real one, so the sysfs walk can be
// exercised with a fixture. Each entry <sysfs_class_tty_dir>/<name> is a
// symlink (or directory) with a "device" entry; USB identity is read from
// the nearest ancestor directory that has an "idVendor" file. The returned
// path is "<dev_dir>/<name>".
std::vector<SerialPortInfo> enumerate_serial_ports_from_sysfs(
    const std::string& sysfs_class_tty_dir,
    const std::string& dev_dir);
#endif
#endif

#ifndef _WIN32
// Test seam: enumerate ports from arbitrary directories rather than the
// platform defaults. Used by unit tests with a tmpfs fixture.
//
// * dev_dir is scanned for entries matching the platform's tty-prefix
//   set (macOS: cu.usbmodem, cu.usbserial, tty.usbmodem, tty.usbserial;
//   Linux: ttyUSB, ttyACM).
//   Matching entries are returned as "<dev_dir>/<name>".
// * by_id_dir, if non-empty, is scanned for all entries (its contents
//   are preserved verbatim as "<by_id_dir>/<name>") -- this mirrors
//   the Linux /dev/serial/by-id behaviour where each symlink is a
//   stable device identifier.
std::vector<std::string> enumerate_ports_from_dirs(
    const std::string& dev_dir,
    const std::string& by_id_dir);
#endif

}  // namespace beebium::serial

#endif  // BEEBIUM_SERIAL_ENUMERATE_PORTS_HPP

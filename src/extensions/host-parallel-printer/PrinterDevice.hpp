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

#ifndef BEEBIUM_EXT_HOST_PARALLEL_PRINTER_DEVICE_HPP
#define BEEBIUM_EXT_HOST_PARALLEL_PRINTER_DEVICE_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beebium::printer {

// A printer device on the host, opened for writing through the operating
// system's plain device-file contract: /dev/usb/lp0 or /dev/lp0 on Linux,
// LPT1 or a USB printer-class path on Windows. Nothing is configured on the
// device (it is not a serial port) and nothing is read from it.
//
// The platform halves are PrinterDevicePosix.cpp and PrinterDeviceWin32.cpp.
// Every method is called on the sink's writer thread, which owns the device.
class PrinterDevice {
public:
    struct WriteResult {
        std::size_t bytes = 0;  // bytes the device took
        bool failed = false;    // the device has gone: close it and go offline
        std::string error;      // why, when failed
    };

    // Open `path` for writing, or return null with the reason in `error`. A
    // device that is absent or switched off fails to open (ENXIO, ENOENT and
    // the like on POSIX; any CreateFile failure on Windows).
    static std::unique_ptr<PrinterDevice> open(const std::string& path, std::string& error);

    // Called once on the thread that will write, before any write. On POSIX it
    // blocks SIGPIPE there, so a write to a device whose far end has gone
    // fails with EPIPE rather than raising a signal that ends the process.
    static void prepare_writer_thread();

    // Unblock a write the given thread is waiting in, so it can be joined.
    // Windows cancels a synchronous WriteFile; on POSIX writes never wait
    // longer than their timeout, so this does nothing.
    static void cancel_blocked_write(std::thread& writer);

    virtual ~PrinterDevice() = default;

    // Write up to `size` bytes, waiting at most about `timeout` for the device
    // to take them. A device that is merely busy takes fewer bytes, or none,
    // and has not failed.
    virtual WriteResult write(const uint8_t* data, std::size_t size,
                              std::chrono::milliseconds timeout) = 0;

    // True if the device reports an error without being written to, such as a
    // FIFO whose reader has gone. Never waits.
    virtual bool has_failed() = 0;
};

// A printer device present on the host, for a user choosing `device`.
struct PrinterDeviceInfo {
    std::string path;  // what to pass as device=
    std::string name;  // a friendly name for it
};

// The printer devices present now. POSIX: the /dev/usb/lp* (usblp) and
// /dev/lp* (parport) nodes. Windows: the interfaces of the USB printer class
// (GUID_DEVINTERFACE_USBPRINT), each a \\?\usb#... path that CreateFileW
// opens; a USB printer is not reachable as LPTn, and the USB001 name the
// Printers dialog shows is a spooler port, not a device. Enumeration only:
// nothing is chosen or opened. Empty, not an error, when there are none.
std::vector<PrinterDeviceInfo> list_printer_devices();

// The path CreateFileW is given for `device` on Windows: a bare LPTn names the
// port through the device namespace, \\.\LPTn; anything else is used as is.
std::string win32_device_path(const std::string& device);

}  // namespace beebium::printer

#endif  // BEEBIUM_EXT_HOST_PARALLEL_PRINTER_DEVICE_HPP

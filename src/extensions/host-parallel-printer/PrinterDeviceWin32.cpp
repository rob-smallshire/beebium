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

// The Windows half of PrinterDevice: CreateFileW and WriteFile on the device.
// A bare LPTn is opened as \\.\LPTn; a USB printer (including a
// USB-to-Centronics cable) is opened by its USBPRINT interface path, which
// list_printer_devices() enumerates through SetupAPI. Any failure to open or to write means
// the printer has gone. WriteFile is synchronous; the sink cancels a write
// still waiting on a printer at teardown with CancelSynchronousIo.

#include "PrinterDevice.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>

#include <limits>
#include <vector>

namespace beebium::printer {

namespace {

std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                             static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                          length);
    return wide;
}

std::string to_utf8(const wchar_t* wide) {
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(length - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), length, nullptr, nullptr);
    return utf8;
}

// GUID_DEVINTERFACE_USBPRINT, from the WDK's usbprint.h, which the SDK lacks.
const GUID kUsbPrintInterface = {
    0x28d78fad, 0x5a12, 0x11d1, {0xae, 0x5b, 0x00, 0x00, 0xf8, 0x03, 0xa8, 0xc2}};

// A string registry property of the device, or empty.
std::string device_property(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property) {
    wchar_t value[256] = {};
    if (!::SetupDiGetDeviceRegistryPropertyW(set, &device, property, nullptr,
                                             reinterpret_cast<PBYTE>(value),
                                             sizeof(value) - sizeof(wchar_t), nullptr)) {
        return {};
    }
    return to_utf8(value);
}

std::string last_error_text() {
    const DWORD code = ::GetLastError();
    char* message = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPSTR>(&message), 0, nullptr);
    std::string text = length > 0 ? std::string(message, length) : std::string();
    if (message != nullptr) {
        ::LocalFree(message);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text.empty() ? "error " + std::to_string(code) : text;
}

class Win32PrinterDevice final : public PrinterDevice {
public:
    explicit Win32PrinterDevice(HANDLE handle) : handle_(handle) {}
    ~Win32PrinterDevice() override { ::CloseHandle(handle_); }

    WriteResult write(const uint8_t* data, std::size_t size,
                      std::chrono::milliseconds /*timeout*/) override {
        WriteResult result;
        const DWORD request = static_cast<DWORD>(
            size < std::numeric_limits<DWORD>::max() ? size : std::numeric_limits<DWORD>::max());
        DWORD written = 0;
        if (!::WriteFile(handle_, data, request, &written, nullptr)) {
            result.failed = true;
            result.error = last_error_text();
        }
        result.bytes = written;
        return result;
    }

    bool has_failed() override { return false; }

private:
    HANDLE handle_;
};

}  // namespace

std::vector<PrinterDeviceInfo> list_printer_devices() {
    std::vector<PrinterDeviceInfo> devices;
    HDEVINFO set = ::SetupDiGetClassDevsW(&kUsbPrintInterface, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        return devices;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!::SetupDiEnumDeviceInterfaces(set, nullptr, &kUsbPrintInterface, index,
                                           &interface_data)) {
            break;  // ERROR_NO_MORE_ITEMS
        }
        DWORD required = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &interface_data, nullptr, 0, &required,
                                           nullptr);
        if (required == 0) {
            continue;
        }
        std::vector<BYTE> buffer(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!::SetupDiGetDeviceInterfaceDetailW(set, &interface_data, detail, required, nullptr,
                                                &device)) {
            continue;
        }
        std::string name = device_property(set, device, SPDRP_FRIENDLYNAME);
        if (name.empty()) {
            name = device_property(set, device, SPDRP_DEVICEDESC);
        }
        devices.push_back({to_utf8(detail->DevicePath), name.empty() ? "USB printer" : name});
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return devices;
}

std::unique_ptr<PrinterDevice> PrinterDevice::open(const std::string& path, std::string& error) {
    const std::string device_path = win32_device_path(path);
    HANDLE handle = ::CreateFileW(to_wide(device_path).c_str(), GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = "cannot open " + device_path + ": " + last_error_text();
        return nullptr;
    }
    return std::make_unique<Win32PrinterDevice>(handle);
}

void PrinterDevice::prepare_writer_thread() {}

void PrinterDevice::cancel_blocked_write(std::thread& writer) {
    if (writer.joinable()) {
        ::CancelSynchronousIo(static_cast<HANDLE>(writer.native_handle()));
    }
}

}  // namespace beebium::printer

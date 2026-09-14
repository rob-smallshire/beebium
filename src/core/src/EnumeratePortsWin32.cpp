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

#ifdef _WIN32

#include "beebium/serial/EnumeratePorts.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <initguid.h>  // instantiate GUID_DEVCLASS_PORTS in this TU
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::serial {

namespace {

// True iff the device-object name looks like a standard COM port:
// "COM" (case-insensitive) followed by one or more decimal digits.
// QueryDosDeviceW returns many device names (A:, MAILSLOT\..., etc.),
// so we filter tightly rather than accepting anything that starts with
// "COM".
bool is_com_name(std::wstring_view name) noexcept {
    if (name.size() < 4) return false;
    if (std::towupper(name[0]) != L'C' ||
        std::towupper(name[1]) != L'O' ||
        std::towupper(name[2]) != L'M') {
        return false;
    }
    for (std::size_t i = 3; i < name.size(); ++i) {
        if (name[i] < L'0' || name[i] > L'9') return false;
    }
    return true;
}

std::string wide_to_utf8(std::wstring_view w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(
        CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
        out.data(), n, nullptr, nullptr);
    return out;
}

// Parse the 4 hex digits following a "<tag>_" marker (e.g. "VID_2E8A")
// anywhere in a device hardware id. Absent or malformed -> nullopt.
std::optional<std::uint16_t> parse_id_after(std::wstring_view hardware_id,
                                            std::wstring_view tag) {
    auto pos = hardware_id.find(tag);
    if (pos == std::wstring_view::npos) return std::nullopt;
    pos += tag.size();
    if (pos + 4 > hardware_id.size()) return std::nullopt;
    std::uint16_t value = 0;
    for (int i = 0; i < 4; ++i) {
        wchar_t c = hardware_id[pos + i];
        int digit;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = 10 + (c - L'a');
        else if (c >= L'A' && c <= L'F') digit = 10 + (c - L'A');
        else return std::nullopt;
        value = static_cast<std::uint16_t>((value << 4) | digit);
    }
    return value;
}

// Read a SetupAPI string device-registry property (SPDRP_*). Empty if the
// property is absent or not a string.
std::string read_string_property(HDEVINFO devinfo, SP_DEVINFO_DATA& data,
                                 DWORD property) {
    DWORD type = 0;
    DWORD bytes = 0;
    ::SetupDiGetDeviceRegistryPropertyW(devinfo, &data, property, &type,
                                        nullptr, 0, &bytes);
    if (bytes == 0) return {};
    std::vector<BYTE> buffer(bytes);
    if (!::SetupDiGetDeviceRegistryPropertyW(devinfo, &data, property, &type,
                                             buffer.data(), bytes, nullptr)) {
        return {};
    }
    if (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ) {
        return {};
    }
    return wide_to_utf8(reinterpret_cast<const wchar_t*>(buffer.data()));
}

// Read the "PortName" (e.g. "COM5") from the device's hardware registry key.
std::string read_port_name(HDEVINFO devinfo, SP_DEVINFO_DATA& data) {
    HKEY key = ::SetupDiOpenDevRegKey(devinfo, &data, DICS_FLAG_GLOBAL, 0,
                                      DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) return {};
    wchar_t name[32] = {};
    DWORD type = 0;
    DWORD bytes = sizeof(name);
    LONG rc = ::RegQueryValueExW(key, L"PortName", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(name), &bytes);
    ::RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return {};
    return wide_to_utf8(name);
}

}  // namespace

std::vector<std::string> enumerate_ports() {
    std::vector<std::string> ports;

    // QueryDosDeviceW(nullptr, buffer, size) reports every device-
    // object name in the global namespace as a double-null-terminated
    // list. Grow the buffer until the call fits; on unrelated errors
    // return whatever we have gathered (empty list from a failed
    // initial call).
    std::vector<wchar_t> buf(1u << 14);
    while (true) {
        DWORD got = ::QueryDosDeviceW(
            nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (got != 0) {
            std::size_t i = 0;
            while (i < buf.size() && buf[i] != L'\0') {
                std::wstring_view name(buf.data() + i);
                if (is_com_name(name)) {
                    ports.push_back(wide_to_utf8(name));
                }
                i += name.size() + 1;
            }
            break;
        }
        if (::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            buf.resize(buf.size() * 2);
            continue;
        }
        break;
    }

    // Lexicographic sort keeps the list stable; note this places COM10
    // between COM1 and COM2 which is mildly counter-intuitive but
    // consistent with the POSIX implementation's sort semantics.
    std::sort(ports.begin(), ports.end());
    ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
    return ports;
}

std::vector<SerialPortInfo> enumerate_serial_ports() {
    std::vector<SerialPortInfo> infos;

    HDEVINFO devinfo = ::SetupDiGetClassDevsW(
        &GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devinfo == INVALID_HANDLE_VALUE) {
        return infos;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD index = 0;
         ::SetupDiEnumDeviceInfo(devinfo, index, &data); ++index) {
        std::string port_name = read_port_name(devinfo, data);
        if (port_name.empty()) continue;  // Not a COM port (LPT, etc.).

        SerialPortInfo info;
        info.path = std::move(port_name);

        // Hardware id carries VID_/PID_ for USB-backed ports (USB-CDC
        // adapters like the Piconet's Pico). Native/legacy COM ports have
        // no such marker; their ids stay empty.
        DWORD type = 0;
        DWORD bytes = 0;
        ::SetupDiGetDeviceRegistryPropertyW(
            devinfo, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &bytes);
        if (bytes > 0) {
            std::vector<BYTE> buffer(bytes);
            if (::SetupDiGetDeviceRegistryPropertyW(
                    devinfo, &data, SPDRP_HARDWAREID, &type,
                    buffer.data(), bytes, nullptr)) {
                std::wstring_view hardware_id(
                    reinterpret_cast<const wchar_t*>(buffer.data()));
                info.usb_vendor_id = parse_id_after(hardware_id, L"VID_");
                info.usb_product_id = parse_id_after(hardware_id, L"PID_");
            }
        }

        info.manufacturer = read_string_property(devinfo, data, SPDRP_MFG);
        info.product = read_string_property(devinfo, data, SPDRP_DEVICEDESC);

        infos.push_back(std::move(info));
    }

    ::SetupDiDestroyDeviceInfoList(devinfo);

    std::sort(infos.begin(), infos.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) {
                  return a.path < b.path;
              });
    infos.erase(std::unique(infos.begin(), infos.end(),
                            [](const SerialPortInfo& a, const SerialPortInfo& b) {
                                return a.path == b.path;
                            }),
                infos.end());
    return infos;
}

}  // namespace beebium::serial

#endif  // _WIN32

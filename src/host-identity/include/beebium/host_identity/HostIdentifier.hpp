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

#ifndef BEEBIUM_HOST_IDENTITY_HOST_IDENTIFIER_HPP
#define BEEBIUM_HOST_IDENTITY_HOST_IDENTIFIER_HPP

// Standard library and OS headers only, so that a client can identify its
// host the same way a server does without the emulator core's include tree.

#include <optional>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#elif defined(__APPLE__)
#include <ctime>
#include <unistd.h>
#include <uuid/uuid.h>
#else
#include <fstream>
#endif

namespace beebium::platform {

// An identifier for the host this process is running on, the same for every
// process on it and different on any other.
//
// This answers one question: are two processes looking at the same
// filesystem? Network addresses cannot answer it. A client reaching a server
// on the same machine may do so over loopback, over the machine's LAN
// address, or through a Bonjour ".local" name that resolves back to itself,
// and all three look different while naming the same host. Comparing host
// identifiers is indifferent to how the connection was made.
//
// A container is reported as a different host, which is the answer that
// matters here: it has its own filesystem, so a path from outside it does not
// mean what the sender intended.
//
// Returns nullopt if the platform will not say. Callers must treat that as
// "not the same host" rather than guessing, since guessing wrong hands one
// machine paths that only exist on another.
inline std::optional<std::string> host_identifier() {
#if defined(_WIN32)
    HKEY key{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Cryptography",
                      0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    char value[128] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG result =
        RegQueryValueExA(key, "MachineGuid", nullptr, &type,
                         reinterpret_cast<LPBYTE>(value), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ || size == 0) {
        return std::nullopt;
    }
    return std::string(value, strnlen(value, sizeof(value)));
#elif defined(__APPLE__)
    uuid_t id{};
    // A null timeout means "do not wait": the value is available immediately
    // in practice, and blocking here would be on a request path.
    const struct timespec no_wait{0, 0};
    if (gethostuuid(id, &no_wait) != 0) {
        return std::nullopt;
    }
    char text[37] = {};
    uuid_unparse_lower(id, text);
    return std::string(text);
#else
    // systemd's machine-id, with the older D-Bus location as a fallback for
    // systems that predate it or do not run systemd.
    for (const char* candidate : {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
        std::ifstream file(candidate);
        std::string id;
        if (file && std::getline(file, id) && !id.empty()) {
            return id;
        }
    }
    return std::nullopt;
#endif
}

}  // namespace beebium::platform

#endif  // BEEBIUM_HOST_IDENTITY_HOST_IDENTIFIER_HPP

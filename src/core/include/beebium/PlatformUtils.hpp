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

#ifndef BEEBIUM_PLATFORM_UTILS_HPP
#define BEEBIUM_PLATFORM_UTILS_HPP

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <mach-o/dyld.h>
#else
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#endif

namespace beebium::platform {

// Cross-platform getpid wrapper.
inline int get_pid() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

// Cross-platform getenv wrapper (avoids MSVC C4996 warning for std::getenv).
inline std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) == 0 && value != nullptr) {
        std::string result(value);
        free(value);
        return result;
    }
    return std::nullopt;
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

// Cross-platform path to the running executable itself.
//
// Resolves the real on-disk location, following symlinks on POSIX, so it is
// robust to a bare argv[0]: when a binary is invoked through a PATH-resolved
// symlink (e.g. /usr/bin/beebium-model-b -> /opt/beebium/bin/...), the shell
// passes only the command name as argv[0], which canonical(argv0) cannot
// resolve. Reading the executable path from the OS avoids depending on argv[0]
// at all. Returns nullopt if the path cannot be determined.
inline std::optional<std::filesystem::path> executable_path() {
#if defined(_WIN32)
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::filesystem::path(path);
    }
    return std::nullopt;
#elif defined(__APPLE__)
    // First call with a null buffer to learn the required size, then fill it.
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        // _NSGetExecutablePath may yield a symlink path; canonicalise so a
        // Homebrew bin/ symlink resolves to the real libexec/ location.
        std::error_code ec;
        auto resolved = std::filesystem::canonical(buffer.c_str(), ec);
        if (!ec) {
            return resolved;
        }
        return std::filesystem::path(buffer.c_str());
    }
    return std::nullopt;
#else
    std::error_code ec;
    auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return self;
    }
    return std::nullopt;
#endif
}

// Cross-platform path to the directory containing the running executable.
// See executable_path() for why this does not consult argv[0].
inline std::optional<std::filesystem::path> executable_directory() {
    if (auto path = executable_path()) {
        return path->parent_path();
    }
    return std::nullopt;
}

// Best-effort flush of a file's contents from the OS cache to the storage
// device, so writes survive a process or OS crash rather than only a clean
// process exit. Uses fsync (POSIX) / FlushFileBuffers (Windows). This is not a
// full hardware barrier (no F_FULLFSYNC), which is the right trade-off for an
// emulator: durable against crashes without the cost of a platter sync on
// every save. Returns false if the file cannot be opened or synced.
inline bool sync_file_to_disk(const std::filesystem::path& filepath) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(filepath.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    bool ok = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    return ok;
#else
    int fd = ::open(filepath.c_str(), O_WRONLY);
    if (fd < 0) return false;
    bool ok = (::fsync(fd) == 0);
    ::close(fd);
    return ok;
#endif
}

// Cross-platform page size query.
inline size_t get_page_size() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}

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

#endif  // BEEBIUM_PLATFORM_UTILS_HPP

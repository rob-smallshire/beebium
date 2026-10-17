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

// The directory a running machine owns, locked for the life of the process.
// The media a template names are copied into it (DiscPaths), so the guest's
// writes change the machine and never the template.
//
// A transient machine's directory is made by its launch, under machines/ in
// the per-user directory, and removed when the machine is shut down cleanly;
// one left by a process that has gone is removed by the next launch. A saved
// machine's directory is wherever the user keeps it: a launch opens it and
// leaves it in place.

#pragma once

#include <beebium/PlatformUtils.hpp>
#include <beebium/server/MachineDocument.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace beebium::server {

// Thrown when another process holds a machine directory's lock: names the
// directory and the holder's process id ("" when it cannot be read).
class MachineInUse : public std::runtime_error {
public:
    MachineInUse(const std::filesystem::path& dirpath, std::string holder)
        : std::runtime_error(dirpath.string() + " is in use by process " +
                             (holder.empty() ? "?" : holder)),
          holder_(std::move(holder)) {}

    const std::string& holder() const { return holder_; }

private:
    std::string holder_;
};

// An exclusive advisory lock on a file, held until destroyed: flock on POSIX,
// LockFileEx on Windows, both released by the system if the process dies.
// The holder's process id is written at the start of the file, outside the
// locked byte range on Windows, so another process can name the holder.
class MachineDirectoryLock {
public:
    // Takes the lock on `lock_filepath`, creating the file. Throws
    // MachineInUse, naming the holder's process id, when another process
    // holds it.
    explicit MachineDirectoryLock(const std::filesystem::path& lock_filepath) {
#ifdef _WIN32
        // Not FILE_SHARE_DELETE: while this handle is open no other process can
        // delete the lock file, so a sweep that released the lock cannot delete
        // a directory another launch has locked since (see sweep).
        handle_ = ::CreateFileW(lock_filepath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("cannot open " + lock_filepath.string());
        }
        OVERLAPPED overlapped{};
        overlapped.Offset = kLockedByteOffset;
        if (!::LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                          &overlapped)) {
            const auto holder = read_holder();
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw_in_use(lock_filepath, holder);
        }
        const std::string pid = std::to_string(::GetCurrentProcessId());
        DWORD written = 0;
        ::SetFilePointer(handle_, 0, nullptr, FILE_BEGIN);
        ::SetEndOfFile(handle_);
        ::WriteFile(handle_, pid.data(), static_cast<DWORD>(pid.size()), &written, nullptr);
#else
        fd_ = ::open(lock_filepath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("cannot open " + lock_filepath.string());
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            const auto holder = read_holder();
            ::close(fd_);
            fd_ = -1;
            throw_in_use(lock_filepath, holder);
        }
        const std::string pid = std::to_string(::getpid());
        if (::ftruncate(fd_, 0) != 0 ||
            ::pwrite(fd_, pid.data(), pid.size(), 0) != static_cast<ssize_t>(pid.size())) {
            // The lock is held; only the message naming the holder is lost.
        }
#endif
    }

    ~MachineDirectoryLock() { release(); }
    MachineDirectoryLock(const MachineDirectoryLock&) = delete;
    MachineDirectoryLock& operator=(const MachineDirectoryLock&) = delete;
    MachineDirectoryLock(MachineDirectoryLock&& other) noexcept {
#ifdef _WIN32
        handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
#else
        fd_ = std::exchange(other.fd_, -1);
#endif
    }
    MachineDirectoryLock& operator=(MachineDirectoryLock&&) = delete;

    void release() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(std::exchange(handle_, INVALID_HANDLE_VALUE));
#else
        if (fd_ >= 0) ::close(std::exchange(fd_, -1));
#endif
    }

private:
    [[noreturn]] static void throw_in_use(const std::filesystem::path& lock_filepath,
                                          const std::string& holder) {
        throw MachineInUse(lock_filepath.parent_path(), holder);
    }

#ifdef _WIN32
    // Past any process id, so the id can be read while the lock is held.
    static constexpr DWORD kLockedByteOffset = 1u << 20;

    std::string read_holder() const {
        char buffer[32] = {};
        DWORD read = 0;
        OVERLAPPED at_start{};
        ::ReadFile(handle_, buffer, sizeof(buffer) - 1, &read, &at_start);
        return std::string(buffer, read);
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    std::string read_holder() const {
        char buffer[32] = {};
        const auto read = ::pread(fd_, buffer, sizeof(buffer) - 1, 0);
        return read > 0 ? std::string(buffer, static_cast<std::size_t>(read)) : std::string();
    }

    int fd_ = -1;
#endif
};

// Who runs the machine at `dirpath`: the holder's process id ("" when it
// cannot be read), or nullopt when no process holds its lock. Finds out by
// taking the lock and at once releasing it, so the id is read only when the
// lock cannot be taken; a directory with no lock file is not run by anyone.
inline std::optional<std::string> machine_lock_holder(const std::filesystem::path& dirpath) {
    const auto lock_filepath = dirpath / machine_layout::LOCK_FILENAME;
    std::error_code ec;
    if (!std::filesystem::exists(lock_filepath, ec)) return std::nullopt;
    try {
        MachineDirectoryLock lock(lock_filepath);
        return std::nullopt;
    } catch (const MachineInUse& e) {
        return e.holder();
    }
}

class MachineDirectory {
public:
    // Where machine directories are made: machines/ in the per-user directory.
    static std::filesystem::path machines_dirpath() {
        return beebium::platform::user_state_base_dirpath() / "machines";
    }

    // A new machine directory for the machine `machine_uuid`, named by it and
    // locked by this process, after removing any left by a process that has
    // gone. Throws when one cannot be made, or another process runs it.
    static MachineDirectory create(const std::string& machine_uuid) {
        sweep(machines_dirpath());
        const auto dirpath = machines_dirpath() / machine_uuid;
        // A sweep running in another launch may take the lock of a directory
        // just made, before its maker does, and remove it; make it again.
        for (int attempt = 0;; ++attempt) {
            try {
                std::filesystem::create_directories(dirpath);
                MachineDirectoryLock lock(dirpath / machine_layout::LOCK_FILENAME);
                if (!std::filesystem::exists(dirpath / machine_layout::LOCK_FILENAME)) {
                    throw std::runtime_error("removed while being made: " + dirpath.string());
                }
                return MachineDirectory(dirpath, std::move(lock));
            } catch (const std::exception&) {
                if (attempt == 2) throw;
            }
        }
    }

    // The saved machine directory at `dirpath`, locked by this process and
    // kept when the machine shuts down. Throws when it is not a directory,
    // or another process runs it.
    static MachineDirectory open(const std::filesystem::path& dirpath) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dirpath, ec)) {
            throw std::runtime_error(dirpath.string() + " is not a machine directory");
        }
        MachineDirectoryLock lock(dirpath / machine_layout::LOCK_FILENAME);
        MachineDirectory directory(dirpath, std::move(lock));
        directory.transient_ = false;
        return directory;
    }

    // Removes every directory under `machines_dirpath` whose lock can be
    // taken -- one whose process has gone -- with one line for each. Never
    // touches anything that is not such a directory.
    static void sweep(const std::filesystem::path& machines_dirpath) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(machines_dirpath, ec)) {
            if (!entry.is_directory(ec)) continue;
            try {
                MachineDirectoryLock lock(entry.path() / machine_layout::LOCK_FILENAME);
#ifdef _WIN32
                // Windows will not delete a file this process has open, and a
                // launch that locks it after this release cannot have it
                // deleted (the lock file is not opened for shared deletion).
                lock.release();
#endif
                // On POSIX, removed under the lock, so no launch can take it
                // between the release and the removal. A launch making a
                // directory can still lose it to a sweep before it locks it;
                // create() checks for that and makes another, so the race is
                // closed by both together.
                clear_directory_readonly(entry.path());
                std::filesystem::remove_all(entry.path(), ec);
                if (!ec) {
                    std::cerr << "Removed the machine directory " << entry.path().string()
                              << ", left by a machine that did not shut down\n";
                }
            } catch (const std::exception&) {
                // In use, or not ours to open: leave it.
            }
        }
    }

    MachineDirectory(MachineDirectory&& other) noexcept
        : dirpath_(std::exchange(other.dirpath_, {})),
          lock_(std::move(other.lock_)),
          transient_(other.transient_) {}
    MachineDirectory& operator=(MachineDirectory&&) = delete;
    MachineDirectory(const MachineDirectory&) = delete;
    MachineDirectory& operator=(const MachineDirectory&) = delete;

    // Releases the lock and, for a transient machine, removes the directory.
    // The machine's media must be closed first (Windows refuses to remove an
    // open file), so the owner destroys this after the machine.
    ~MachineDirectory() {
        if (dirpath_.empty()) return;
        lock_.release();
        if (!transient_) return;
        std::error_code ec;
        clear_directory_readonly(dirpath_);
        std::filesystem::remove_all(dirpath_, ec);
        if (ec) {
            std::cerr << "Warning: cannot remove the machine directory " << dirpath_.string()
                      << ": " << ec.message() << "\n";
        }
    }

    const std::filesystem::path& dirpath() const { return dirpath_; }

    // True for a directory the launch made and removes at a clean shutdown.
    bool transient() const { return transient_; }

private:
    MachineDirectory(std::filesystem::path dirpath, MachineDirectoryLock lock)
        : dirpath_(std::move(dirpath)), lock_(std::move(lock)) {}

    std::filesystem::path dirpath_;
    MachineDirectoryLock lock_;
    bool transient_ = true;
};

}  // namespace beebium::server

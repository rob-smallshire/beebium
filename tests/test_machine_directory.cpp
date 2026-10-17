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

// test_machine_directory.cpp
// Tests for the directory a running machine owns (MachineDirectory.hpp): made,
// locked and removed in this process, and locked by a real server in another.

#include <beebium/server/MachineDirectory.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef BEEBIUM_SERVER_FILEPATH
#error "BEEBIUM_SERVER_FILEPATH must be defined"
#endif

using namespace beebium::server;
namespace fs = std::filesystem;

namespace {

// The directories under `machines_dirpath`.
std::vector<fs::path> machine_dirpaths(const fs::path& machines_dirpath) {
    std::vector<fs::path> dirpaths;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(machines_dirpath, ec)) {
        if (entry.is_directory()) dirpaths.push_back(entry.path());
    }
    return dirpaths;
}

std::string read_text(const fs::path& filepath) {
    std::ifstream in(filepath);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string current_pid() {
#ifdef _WIN32
    return std::to_string(::GetCurrentProcessId());
#else
    return std::to_string(::getpid());
#endif
}

// What `body` writes to std::cerr.
template <typename F>
std::string cerr_of(F body) {
    std::ostringstream captured;
    auto* previous = std::cerr.rdbuf(captured.rdbuf());
    body();
    std::cerr.rdbuf(previous);
    return captured.str();
}

// A server started by this test, waiting for its API (so it runs until it is
// stopped), with its output discarded. Only ever stops the process it started.
class ServerChild {
public:
    ServerChild() {
#ifdef _WIN32
        std::wstring command = L"\"" + fs::path(BEEBIUM_SERVER_FILEPATH).wstring() +
                               L"\" start --wait=api --port 0";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        REQUIRE(::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_));
        pid_ = std::to_string(process_.dwProcessId);
#else
        pid_t pid = ::fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            const int null_fd = ::open("/dev/null", O_WRONLY);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            ::execl(BEEBIUM_SERVER_FILEPATH, BEEBIUM_SERVER_FILEPATH, "start", "--wait=api",
                    "--port", "0", static_cast<char*>(nullptr));
            ::_exit(127);
        }
        pid_value_ = pid;
        pid_ = std::to_string(pid);
#endif
    }

    ~ServerChild() { kill(); }
    ServerChild(const ServerChild&) = delete;
    ServerChild& operator=(const ServerChild&) = delete;

    const std::string& pid() const { return pid_; }

    // Asks the server to stop as a user would, and waits for it. POSIX only:
    // Windows has no such signal for a console-less child.
#ifndef _WIN32
    int terminate() {
        ::kill(pid_value_, SIGTERM);
        return wait();
    }
#endif

    // Stops the server as a crash would, and waits for it.
    void kill() {
#ifdef _WIN32
        if (process_.hProcess) {
            ::TerminateProcess(process_.hProcess, 1);
            ::WaitForSingleObject(process_.hProcess, INFINITE);
            ::CloseHandle(process_.hProcess);
            ::CloseHandle(process_.hThread);
            process_ = {};
        }
#else
        if (pid_value_ > 0) {
            ::kill(pid_value_, SIGKILL);
            wait();
        }
#endif
    }

private:
#ifndef _WIN32
    int wait() {
        int status = 0;
        ::waitpid(pid_value_, &status, 0);
        pid_value_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    pid_t pid_value_ = -1;
#else
    PROCESS_INFORMATION process_{};
#endif
    std::string pid_;
};

// The lock file of the one machine directory under `machines_dirpath` once
// a process has written its id there, waiting up to 30 seconds.
std::optional<fs::path> wait_for_locked_machine(const fs::path& machines_dirpath) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        for (const auto& dirpath : machine_dirpaths(machines_dirpath)) {
            if (!read_text(dirpath / "lock").empty()) return dirpath / "lock";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("MachineDirectory: made under machines/, locked by this process, removed when done",
          "[machine_directory]") {
    beebium::test::ScopedTempDir user("beebium_machine_dir_");
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user.path().string());
    fs::path dirpath;
    {
        auto machine = MachineDirectory::create();
        dirpath = machine.dirpath();
        CHECK(dirpath.parent_path() == user / "machines");
        CHECK(read_text(dirpath / "lock") == current_pid());
        std::ofstream(dirpath / "game.ssd") << "copy";
    }
    CHECK_FALSE(fs::exists(dirpath));
}

TEST_CASE("MachineDirectory: a directory whose process has gone is swept, one line each",
          "[machine_directory]") {
    beebium::test::ScopedTempDir user("beebium_machine_dir_");
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user.path().string());
    const auto machines = user / "machines";
    fs::create_directories(machines / "left-by-a-crash");
    std::ofstream(machines / "left-by-a-crash" / "lock") << "99999";
    std::ofstream(machines / "not-a-directory") << "kept";

    const auto said = cerr_of([&] { MachineDirectory::sweep(machines); });
    CHECK_FALSE(fs::exists(machines / "left-by-a-crash"));
    CHECK(fs::exists(machines / "not-a-directory"));
    CHECK_THAT(said, Catch::Matchers::ContainsSubstring(
                         (machines / "left-by-a-crash").string()));
}

TEST_CASE("MachineDirectory: a running server's directory is locked against another process",
          "[machine_directory][integration]") {
    beebium::test::ScopedTempDir user("beebium_machine_dir_");
    beebium::test::ScopedEnv user_env("BEEBIUM_USER_DIRPATH", user.path().string());
    const auto machines = user / "machines";
    ServerChild server;
    const auto lock_filepath = wait_for_locked_machine(machines);
    REQUIRE(lock_filepath.has_value());
    CHECK(read_text(*lock_filepath) == server.pid());

    // Another process cannot take it, and is told who holds it...
    CHECK_THROWS_WITH(MachineDirectoryLock(*lock_filepath),
                      Catch::Matchers::ContainsSubstring("in use by process " + server.pid()));
    // ...and a sweep leaves it alone.
    MachineDirectory::sweep(machines);
    CHECK(fs::exists(lock_filepath->parent_path()));

#ifndef _WIN32
    SECTION("a server stopped cleanly removes its directory") {
        CHECK(server.terminate() == 0);
        CHECK_FALSE(fs::exists(lock_filepath->parent_path()));
    }
#endif
    SECTION("a server that dies leaves its directory to the next sweep") {
        server.kill();
        CHECK(fs::exists(lock_filepath->parent_path()));
        MachineDirectory::sweep(machines);
        CHECK_FALSE(fs::exists(lock_filepath->parent_path()));
    }
}

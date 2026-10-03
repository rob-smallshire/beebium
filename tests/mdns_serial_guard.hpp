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

#ifndef BEEBIUM_TESTS_MDNS_SERIAL_GUARD_HPP
#define BEEBIUM_TESTS_MDNS_SERIAL_GUARD_HPP

// Cross-process serial guard for tests that exercise the real system mDNS
// responder (issue #164). Several such processes browsing and registering at
// once saturate the responder: discovery events are then delayed by up to a
// minute, and every discovery-dependent wait times out at once (the failure is
// uniform across unrelated discovery paths, escalates with cumulative
// concurrent load, and clears after the responder sits idle -- the signature of
// resource exhaustion, not a race in our code). This Catch2 listener holds a
// machine-wide advisory file lock for the duration of each test case tagged
// [.mdns] or [stress], so concurrent test binaries queue instead of stampeding.
//
// It uses the same flock / LockFileEx primitive as platform::with_locked_file
// (#161), but held across the case (start..end) which that scoped wrapper
// cannot span, so it stays a small self-contained RAII here in the test support
// rather than reshaping the production helper. Registered only in the
// real-responder binaries (test_aun_mdns_e2e, test_aun_auto_station,
// test_advertiser, test_browser), so an unrelated [stress] test -- keyboard
// typing, say -- is never serialised.

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace beebium::test {

// The one lock file every real-responder test process on this host contends on.
inline std::filesystem::path mdns_responder_lock_filepath() {
    return std::filesystem::temp_directory_path() / "beebium-mdns-responder.lock";
}

// A test case needs the real responder when it is tagged [.mdns] or [stress].
// Catch2 stores each tag without its brackets; the hidden marker keeps the tag
// its leading dot (".mdns").
inline bool test_needs_real_responder(const Catch::TestCaseInfo& info) {
    for (const auto& tag : info.tags) {
        std::string text(tag.original.data(), tag.original.size());
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (text == ".mdns" || text == "mdns" || text == "stress") {
            return true;
        }
    }
    return false;
}

// Holds the machine-wide responder lock for the lifetime of each real-responder
// test case. Acquisition is a bounded wait: if another run holds the lock too
// long, it gives up with a clear note and proceeds unlocked rather than failing
// the case (the test then races the responder as before -- degraded, not
// broken).
class MdnsResponderSerialGuard : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        if (test_needs_real_responder(info)) {
            acquire();
        }
    }

    void testCaseEnded(const Catch::TestCaseStats& /*stats*/) override {
        release();
    }

private:
    static constexpr std::chrono::seconds kTimeout{180};

    void acquire() {
        const auto lock_filepath = mdns_responder_lock_filepath();
        const auto deadline = std::chrono::steady_clock::now() + kTimeout;
#ifdef _WIN32
        handle_ = CreateFileW(lock_filepath.wstring().c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            return;  // best effort
        }
        for (;;) {
            OVERLAPPED overlapped{};
            if (LockFileEx(handle_,
                           LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                           1, 0, &overlapped)) {
                held_ = true;
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                report_timeout();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#else
        fd_ = ::open(lock_filepath.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd_ < 0) {
            return;  // best effort
        }
        for (;;) {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                held_ = true;
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                report_timeout();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
#endif
    }

    void release() {
#ifdef _WIN32
        if (held_) {
            OVERLAPPED overlapped{};
            UnlockFileEx(handle_, 0, 1, 0, &overlapped);
            held_ = false;
        }
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (held_) {
            ::flock(fd_, LOCK_UN);
            held_ = false;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
    }

    static void report_timeout() {
        std::cerr << "[mdns-serial-guard] waited " << kTimeout.count()
                  << "s for the real-responder lock and gave up; running this "
                     "case unlocked. Another real-mDNS test run is probably "
                     "still active -- if this case then times out on discovery, "
                     "rerun it once the others finish.\n";
    }

#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    bool held_ = false;
};

}  // namespace beebium::test

#endif  // BEEBIUM_TESTS_MDNS_SERIAL_GUARD_HPP

// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include <beebium/PacingClock.hpp>
#include <beebium/ThreadName.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>

#if defined(__linux__)
#include <filesystem>
#include <fstream>
#endif

using namespace beebium;

#if defined(__linux__) || defined(__APPLE__)

namespace {

std::string current_thread_name() {
    char name[64] = {};
    pthread_getname_np(pthread_self(), name, sizeof name);
    return name;
}

}  // namespace

TEST_CASE("set_current_thread_name names the calling thread", "[thread_name]") {
    std::string observed;
    std::thread worker([&] {
        set_current_thread_name("render-loop");
        observed = current_thread_name();
    });
    worker.join();
    CHECK(observed == "render-loop");
}

#endif

#if defined(__linux__)

namespace {

bool process_has_thread_named(const std::string& wanted) {
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/task")) {
        std::ifstream comm(entry.path() / "comm");
        std::string name;
        std::getline(comm, name);
        if (name == wanted) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("The pacing clock's timer thread is named pacing-timer", "[thread_name][pacing]") {
    PacingConfig config{2'000'000, 500, 1.0};
    PacingClock clock(config, std::chrono::microseconds(500), PlatformSleep{});
    clock.start();
    // The thread names itself as its first action; give it a moment to run.
    bool named = false;
    for (int attempt = 0; attempt < 200 && !named; ++attempt) {
        named = process_has_thread_named("pacing-timer");
        if (!named) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    clock.stop();
    CHECK(named);
}

#endif

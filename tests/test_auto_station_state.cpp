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

// The per-host auto-station hint file (issue #161) and the portable file lock
// it uses. All tests point at a temporary file, never the user's.

#include <catch2/catch_test_macros.hpp>

#include <beebium/PlatformUtils.hpp>
#include <beebium/econet/AutoStationState.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace beebium::econet;
using namespace std::chrono_literals;

namespace {

std::filesystem::path temp_state_path() {
    std::random_device rd;
    return std::filesystem::temp_directory_path() /
           ("beebium-auto-state-" + std::to_string(rd()) + ".txt");
}

struct TempState {
    std::filesystem::path path = temp_state_path();
    ~TempState() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path.string() + ".lock", ec);
    }
};

}  // namespace

TEST_CASE("advance_auto_station_start: first run starts at range.lo",
          "[auto-station-state]") {
    TempState s;
    auto first = advance_auto_station_start(s.path, {1, 253}, 500ms);
    REQUIRE(first.has_value());
    CHECK(int(*first) == 1);
}

TEST_CASE("advance_auto_station_start: successive launches advance monotonically",
          "[auto-station-state]") {
    TempState s;
    CHECK(int(*advance_auto_station_start(s.path, {1, 253}, 500ms)) == 1);
    CHECK(int(*advance_auto_station_start(s.path, {1, 253}, 500ms)) == 2);
    CHECK(int(*advance_auto_station_start(s.path, {1, 253}, 500ms)) == 3);
}

TEST_CASE("advance_auto_station_start: wraps at the top of the range",
          "[auto-station-state]") {
    TempState s;
    CHECK(int(*advance_auto_station_start(s.path, {80, 82}, 500ms)) == 80);
    CHECK(int(*advance_auto_station_start(s.path, {80, 82}, 500ms)) == 81);
    CHECK(int(*advance_auto_station_start(s.path, {80, 82}, 500ms)) == 82);
    CHECK(int(*advance_auto_station_start(s.path, {80, 82}, 500ms)) == 80);  // wrap
}

TEST_CASE("advance_auto_station_start: a stored value outside the range restarts at lo",
          "[auto-station-state]") {
    TempState s;
    { std::ofstream(s.path) << "200\n"; }
    CHECK(int(*advance_auto_station_start(s.path, {80, 90}, 500ms)) == 80);
}

TEST_CASE("record_auto_station then advance continues past the recorded number",
          "[auto-station-state]") {
    TempState s;
    record_auto_station(s.path, 42, 500ms);
    CHECK(int(*advance_auto_station_start(s.path, {1, 253}, 500ms)) == 43);
}

TEST_CASE("advance_auto_station_start: a corrupt file falls back and self-heals",
          "[auto-station-state]") {
    TempState s;
    { std::ofstream(s.path) << "not-a-number\n"; }
    // Corrupt -> nullopt (the caller falls back to lowest-free this launch).
    CHECK_FALSE(advance_auto_station_start(s.path, {1, 253}, 500ms).has_value());
    // It was reset, so the next launch recovers and advances normally.
    auto recovered = advance_auto_station_start(s.path, {1, 253}, 500ms);
    REQUIRE(recovered.has_value());
    CHECK(int(*recovered) == 2);
}

TEST_CASE("advance_auto_station_start: unwritable directory falls back",
          "[auto-station-state]") {
    // A path under a file (not a directory) cannot be created or opened.
    TempState s;
    { std::ofstream(s.path) << "x"; }
    std::filesystem::path under_file = s.path / "nested" / "state.txt";
    CHECK_FALSE(advance_auto_station_start(under_file, {1, 253}, 200ms).has_value());
}

TEST_CASE("advance_auto_station_start: concurrent advances yield distinct values",
          "[auto-station-state]") {
    TempState s;
    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::vector<int> results(kThreads, -1);
    std::atomic<bool> go{false};
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] {
            while (!go.load()) { /* barrier */ }
            auto v = advance_auto_station_start(s.path, {1, 253}, 2000ms);
            results[i] = v.has_value() ? int(*v) : -1;
        });
    }
    go.store(true);
    for (auto& t : threads) t.join();

    std::set<int> distinct(results.begin(), results.end());
    // The lock serialises the read-advance-write, so every launch gets a
    // different number -- no two inherit the same start.
    CHECK(distinct.size() == static_cast<std::size_t>(kThreads));
    CHECK(distinct.count(-1) == 0);
}

TEST_CASE("with_locked_file: a second caller times out while the lock is held",
          "[auto-station-state][lock]") {
    TempState s;
    const auto lock = s.path.string() + ".lock";
    std::atomic<bool> holding{false};
    std::atomic<bool> release{false};
    std::thread holder([&] {
        beebium::platform::with_locked_file(lock, 500ms, [&] {
            holding.store(true);
            while (!release.load()) {
                std::this_thread::sleep_for(5ms);
            }
        });
    });
    while (!holding.load()) {
        std::this_thread::sleep_for(1ms);
    }
    // The lock is held; a short-timeout attempt must fail without running fn.
    bool ran = false;
    bool acquired =
        beebium::platform::with_locked_file(lock, 50ms, [&] { ran = true; });
    CHECK_FALSE(acquired);
    CHECK_FALSE(ran);
    release.store(true);
    holder.join();
    // Once released, it can be taken.
    bool ran2 = false;
    CHECK(beebium::platform::with_locked_file(lock, 500ms, [&] { ran2 = true; }));
    CHECK(ran2);
}

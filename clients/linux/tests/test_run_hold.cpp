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

// Holding a launched machine's Run until its sound can be heard, so the
// first thing it says, the boot beep, is not lost to a sound output still
// starting (client specification 5.5, 8.3).

#include "app/RunHold.hpp"

#include "WaitUntil.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

TEST_CASE("With the sound ready, Run goes at once", "[run][hold]") {
    RunHold hold;
    int runs = 0;
    hold.arm(true, [&] { ++runs; });
    CHECK(runs == 1);
    CHECK_FALSE(hold.held());
}

TEST_CASE("Run waits for the sound, then goes once", "[run][hold]") {
    RunHold hold;
    int runs = 0;
    hold.arm(false, [&] { ++runs; });
    CHECK(runs == 0);
    CHECK(hold.held());
    hold.release();
    CHECK(runs == 1);
    hold.release();
    CHECK(runs == 1);
    CHECK_FALSE(hold.held());
}

TEST_CASE("Sound that never comes holds Run only as long as the bound", "[run][hold]") {
    RunHold hold;
    int runs = 0;
    QElapsedTimer clock;
    clock.start();
    hold.arm(false, [&] { ++runs; }, 100);
    REQUIRE(wait_until([&] { return runs == 1; }));
    CHECK(clock.elapsed() >= 100);
    hold.release();
    CHECK(runs == 1);
}

TEST_CASE("A connection gone before the sound came never runs", "[run][hold]") {
    RunHold hold;
    int runs = 0;
    hold.arm(false, [&] { ++runs; }, 50);
    hold.cancel();
    hold.release();
    CHECK_FALSE(wait_until([&] { return runs > 0; }, std::chrono::milliseconds(200)));
    CHECK(runs == 0);
}

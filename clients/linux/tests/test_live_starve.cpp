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

// A live input failing to keep up, told once (client specification 9.0,
// Live input; cassette round 11).

#include "app/LiveStarve.hpp"

#include <QLocale>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

using Condition = LiveStarveDetector::Condition;

// One second of status: the overrun and underrun counts since the stream
// began.
struct Second {
    std::uint64_t overruns;
    std::uint64_t underruns;
};

// What each second raised, in order.
std::vector<Condition> run(LiveStarveDetector& detector, std::initializer_list<Second> seconds) {
    std::vector<Condition> raised;
    for (const Second& s : seconds) {
        raised.push_back(detector.feed(s.overruns, s.underruns));
    }
    return raised;
}

constexpr auto NONE = Condition::none;
constexpr auto UNDER = Condition::underrun;
constexpr auto OVER = Condition::overrun;

}  // namespace

TEST_CASE("Three consecutive seconds of growth are the condition", "[live][starve]") {
    STATIC_CHECK(LiveStarveDetector::LIVE_STARVE_SECONDS == 3);
    LiveStarveDetector d;
    CHECK(run(d, {{0, 0}, {0, 10}, {0, 20}, {0, 30}}) ==
          std::vector<Condition>{NONE, NONE, NONE, UNDER});
    LiveStarveDetector o;
    CHECK(run(o, {{0, 0}, {5, 0}, {9, 0}, {12, 0}}) ==
          std::vector<Condition>{NONE, NONE, NONE, OVER});
}

TEST_CASE("The first second is only the baseline, whatever its counts", "[live][starve]") {
    // A stream whose first status already carries counts has not yet been
    // seen to grow: that takes three more seconds.
    LiveStarveDetector d;
    CHECK(run(d, {{5, 400}, {9, 800}, {12, 1200}}) == std::vector<Condition>(3, NONE));
    CHECK(d.feed(16, 1200) == OVER);
    LiveStarveDetector u;
    CHECK(run(u, {{0, 400}, {0, 800}, {0, 1200}, {0, 1600}}) ==
          std::vector<Condition>{NONE, NONE, NONE, UNDER});
}

TEST_CASE("Two seconds of growth are not, nor growth with a quiet second between",
          "[live][starve]") {
    LiveStarveDetector d;
    // A late chunk or two while the ring primes.
    CHECK(run(d, {{0, 0}, {0, 10}, {0, 20}, {0, 20}, {0, 30}, {0, 40}, {0, 40}}) ==
          std::vector<Condition>(7, NONE));
}

TEST_CASE("A raised condition says nothing more while it goes on", "[live][starve]") {
    LiveStarveDetector d;
    CHECK(run(d, {{0, 0}, {0, 10}, {0, 20}, {0, 30}}).back() == UNDER);
    CHECK(run(d, {{0, 40}, {0, 50}, {0, 60}, {0, 70}, {0, 80}}) == std::vector<Condition>(5, NONE));
}

TEST_CASE("A raised condition is raised again only once it has cleared for three seconds and recurs",
          "[live][starve]") {
    LiveStarveDetector d;
    CHECK(run(d, {{0, 0}, {0, 10}, {0, 20}, {0, 30}}).back() == UNDER);
    // Two quiet seconds are not a clearing.
    CHECK(run(d, {{0, 30}, {0, 30}, {0, 40}, {0, 50}, {0, 60}}) == std::vector<Condition>(5, NONE));
    // Three are; then three more of growth raise it again.
    CHECK(run(d, {{0, 60}, {0, 60}, {0, 60}, {0, 70}, {0, 80}, {0, 90}}) ==
          std::vector<Condition>{NONE, NONE, NONE, NONE, NONE, UNDER});
}

TEST_CASE("The other condition replaces the one raised", "[live][starve]") {
    LiveStarveDetector d;
    CHECK(run(d, {{0, 0}, {0, 10}, {0, 20}, {0, 30}}).back() == UNDER);
    CHECK(run(d, {{4, 40}, {8, 50}, {12, 60}}) == std::vector<Condition>{NONE, NONE, OVER});
    // And the first, still growing, does not take its place back.
    CHECK(run(d, {{16, 70}, {16, 80}, {16, 90}, {16, 100}}) == std::vector<Condition>(4, NONE));
}

TEST_CASE("A new stream starts clean", "[live][starve]") {
    LiveStarveDetector d;
    CHECK(run(d, {{0, 0}, {0, 10}, {0, 20}, {0, 30}}).back() == UNDER);
    d.reset();
    // Its counts start again from nothing, and the condition may be told.
    CHECK(run(d, {{0, 0}, {0, 5}, {0, 6}, {0, 7}}) ==
          std::vector<Condition>{NONE, NONE, NONE, UNDER});
}

TEST_CASE("Each condition is told in its own sentence", "[live][starve]") {
    CHECK(live_starve_sentence(UNDER) ==
          QStringLiteral("The live input is not keeping up with the Beeb; some of the tape is being "
                         "made up as silence."));
    CHECK(live_starve_sentence(OVER) ==
          QStringLiteral("The live input is arriving faster than the Beeb can take it; some of the "
                         "tape is being dropped."));
}

TEST_CASE("The detail gives the counts as they were, in samples dropped or seconds made up",
          "[live][starve]") {
    const QLocale english(QLocale::English, QLocale::UnitedKingdom);
    CHECK(live_starve_detail(OVER, 1204, 99, 44100, english) ==
          QStringLiteral("Dropped 1,204 samples since the stream began."));
    CHECK(live_starve_detail(OVER, 1, 0, 44100, english) ==
          QStringLiteral("Dropped 1 sample since the stream began."));
    CHECK(live_starve_detail(UNDER, 7, 38400, 48000, english) ==
          QStringLiteral("Made up 0.8 s of silence since the stream began."));
    CHECK(live_starve_detail(UNDER, 0, 1323000, 44100, english) ==
          QStringLiteral("Made up 30.0 s of silence since the stream began."));
    // A rate not known: the samples, then.
    CHECK(live_starve_detail(UNDER, 0, 512, 0, english) ==
          QStringLiteral("Made up 512 samples of silence since the stream began."));
}

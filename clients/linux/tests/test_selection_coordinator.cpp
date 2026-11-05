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

// The selection's lifecycle (client specification 6.5), against stand-ins
// for the server, the display and the clipboard: the screen freezes, is
// held and shown still, reads name the hold, the highlight follows the
// interpretation, copies read the held still or the whole live screen, and
// every way out releases the hold and lets the display run on.

#include "app/SelectionCoordinator.hpp"

#include "SelectionFakes.hpp"

#include "video.pb.h"

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

TEST_CASE("A selection freezes the picture and owns the modifiers", "[selection]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    CHECK_FALSE(coordinator.selecting());
    CHECK(coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling()));
    CHECK(coordinator.selecting());
    CHECK(f.frozen == 1);
    CHECK(f.keys_released == 1);
    CHECK(coordinator.mapping().has_value());
}

TEST_CASE("No selection starts without a frame, a connection or a point on the picture",
          "[selection]") {
    Fakes f;
    f.live.reset();
    SelectionCoordinator coordinator(f.services());
    CHECK_FALSE(coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling()));
    f.live = filling_content();
    f.connected = false;
    CHECK_FALSE(coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling()));
    f.connected = true;
    // Pillarboxed: the left edge of the view is not the picture.
    CHECK_FALSE(coordinator.begin({10, 500}, Interpretation::rows, filling(2000, 1000)));
    CHECK_FALSE(coordinator.selecting());
    CHECK(f.frozen == 0);
    CHECK(f.pending_holds.empty());
}

TEST_CASE("Cancelling resumes the display and clears, once", "[selection]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    coordinator.cancel();
    CHECK_FALSE(coordinator.selecting());
    CHECK(f.resumed == 1);
    CHECK_FALSE(coordinator.mapping().has_value());
    coordinator.cancel();
    CHECK(f.resumed == 1);
}

TEST_CASE("Rows show their range and no marquee; anywhere a marquee and no range",
          "[selection][overlay]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.3, 0.3), Interpretation::rows, filling());
    f.drain();
    coordinator.update(at(0.7, 0.7));
    CHECK_FALSE(coordinator.overlay().marquee);
    CHECK_FALSE(coordinator.overlay().range.empty());

    coordinator.begin(at(0.3, 0.3), Interpretation::anywhere, filling());
    f.drain();
    coordinator.update(at(0.7, 0.7));
    CHECK(coordinator.overlay().marquee);
    CHECK(coordinator.overlay().range.empty());
}

TEST_CASE("A selection holds the screen and shows the held still", "[selection][hold]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    f.drain();
    CHECK(f.holds == 1);
    CHECK(coordinator.hold_id() == std::uint64_t{42});
    CHECK(f.shown == std::vector<std::uint32_t>{640});
}

TEST_CASE("Reads are made against the held screen", "[selection][hold]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::rows, filling());
    f.drain();
    coordinator.update(at(0.8, 0.8));
    f.drain();
    f.reads.clear();
    coordinator.copy(Interpretation::rows);
    f.drain();
    REQUIRE_FALSE(f.reads.empty());
    for (const auto& read : f.reads) {
        REQUIRE(read.has_hold_id());
        CHECK(read.hold_id() == 42);
    }
}

TEST_CASE("Cancelling releases the hold", "[selection][hold]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    f.drain();
    coordinator.cancel();
    CHECK(f.released == std::vector<std::uint64_t>{42});
}

TEST_CASE("A refused hold ends the selection rather than reading live", "[selection][hold]") {
    Fakes f;
    f.refuse_hold = true;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    f.drain();
    CHECK_FALSE(coordinator.selecting());
    CHECK(f.resumed == 1);
    CHECK(f.reads.empty());
}

TEST_CASE("A hold that answers after its selection ended is released", "[selection][hold]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    coordinator.cancel();
    f.drain();
    CHECK(f.released == std::vector<std::uint64_t>{42});
    CHECK(f.shown.empty());
}

TEST_CASE("A fresh drag releases what the last one held", "[selection][hold]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    f.drain();
    f.next_hold = 43;
    coordinator.begin(at(0.4, 0.4), Interpretation::rows, filling());
    f.drain();
    CHECK(f.released == std::vector<std::uint64_t>{42});
    CHECK(f.holds == 2);
    CHECK(coordinator.hold_id() == std::uint64_t{43});
}

TEST_CASE("A hold gone server-side ends the selection", "[selection][hold]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::rows, filling());
    f.drain();
    f.read_failure = ScreenReadFailure{true, "no screen is held under that id"};
    coordinator.update(at(0.8, 0.8));
    f.drain();
    CHECK_FALSE(coordinator.selecting());
    CHECK(f.resumed == 1);
    CHECK(f.holds_lost == 1);
    // A copy that finds the hold gone ends the selection the same way.
    f.read_failure.reset();
    coordinator.begin(at(0.2, 0.2), Interpretation::columns, filling());
    f.drain();
    f.read_failure = ScreenReadFailure{true, "no screen is held under that id"};
    coordinator.copy(Interpretation::columns);
    f.drain();
    CHECK_FALSE(coordinator.selecting());
    CHECK(f.holds_lost == 2);
    CHECK(f.failures.empty());
}

TEST_CASE("The mapping follows the window, not the drag's start", "[selection][mapping]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.5, 0.5), Interpretation::rows, filling());
    f.drain();
    coordinator.set_presentation(filling(2000, 800));
    const auto mapping = coordinator.mapping();
    REQUIRE(mapping);
    // Pillarboxed now: 800 tall at 1.25 is 1000 wide, centred.
    CHECK(mapping->picture_rect().x == 500);
    CHECK(mapping->picture_rect().width == 1000);
}

TEST_CASE("Copy with no selection copies the whole live screen, aligned", "[selection][copy]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.copy(Interpretation::rows);
    f.drain();
    CHECK(f.clipboard == std::vector<std::string>{"HELLO"});
    REQUIRE(f.reads.size() == 1);
    CHECK_FALSE(f.reads[0].has_region());
    CHECK_FALSE(f.reads[0].has_hold_id());
    CHECK(f.reads[0].search() == beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    CHECK(f.holds == 0);
}

TEST_CASE("Copy as Columns reads the snapped block, aligned", "[selection][copy]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::columns, filling());
    coordinator.update(at(0.8, 0.8));
    f.drain();
    f.reads.clear();
    coordinator.copy(Interpretation::columns);
    f.drain();
    CHECK(f.clipboard.back() == "HELLO");
    REQUIRE(f.reads.size() == 1);
    CHECK(f.reads[0].search() == beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    REQUIRE(f.reads[0].has_region());
    // Columns 16 to 64 of rows 6 to 25 of the eight-pixel grid.
    CHECK(f.reads[0].region().x() == 128);
    CHECK(f.reads[0].region().y() == 48);
    CHECK(f.reads[0].region().width() == 392);
    CHECK(f.reads[0].region().height() == 160);
}

TEST_CASE("Copy Text from Graphics reads the marquee, anywhere", "[selection][copy]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::anywhere, filling());
    coordinator.update(at(0.8, 0.8));
    f.drain();
    f.reads.clear();
    coordinator.copy(Interpretation::anywhere);
    f.drain();
    CHECK(f.clipboard.back() == "HELLO");
    REQUIRE(f.reads.size() == 1);
    CHECK(f.reads[0].search() == beebium::SCREEN_TEXT_SEARCH_ANYWHERE);
    CHECK(f.reads[0].region().x() == 128);
    CHECK(f.reads[0].region().width() == 384);
}

TEST_CASE("Copy reads rows at full width and trims them to the flow", "[selection][copy]") {
    Fakes f;
    f.reading.text = "unused";
    TextRun first;
    first.text = std::string(80, 'A');
    first.bounds = {0, 48, 640, 8};
    first.cell_width = 8;
    TextRun last;
    last.text = std::string(80, 'B');
    last.bounds = {0, 56, 640, 8};
    last.cell_width = 8;
    f.reading.runs = {first, last};
    SelectionCoordinator coordinator(f.services());
    // From column 76 of row 6 to column 3 of row 7.
    coordinator.begin({612 * 1250.0 / 640, 50 * 1000.0 / 256}, Interpretation::rows, filling());
    coordinator.update({30 * 1250.0 / 640, 60 * 1000.0 / 256});
    f.drain();
    f.reads.clear();
    coordinator.copy(Interpretation::rows);
    f.drain();
    REQUIRE(f.reads.size() == 1);
    CHECK(f.reads[0].region().x() == 0);
    CHECK(f.reads[0].region().width() == 640);
    CHECK(f.reads[0].search() == beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    CHECK(f.clipboard.back() == "AAAA\nBBBB");
}

TEST_CASE("A copy asked before the hold answers waits for it", "[selection][copy]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::columns, filling());
    coordinator.copy(Interpretation::columns);
    CHECK(f.reads.empty());
    f.drain();
    CHECK(f.clipboard == std::vector<std::string>{"HELLO"});
    for (const auto& read : f.reads) {
        CHECK(read.hold_id() == 42);
    }
}

TEST_CASE("Copies ask for character codes unless displayed characters are chosen",
          "[selection][copy][mode7]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.copy(Interpretation::rows);
    f.drain();
    CHECK(f.reads.back().characters() == beebium::SCREEN_TEXT_CHARACTERS_CODES);

    // Read afresh at each copy, by every copy.
    f.mode7 = Mode7Characters::displayed;
    f.reads.clear();
    coordinator.copy(Interpretation::rows);
    coordinator.begin(at(0.2, 0.2), Interpretation::columns, filling());
    coordinator.update(at(0.8, 0.8));
    f.drain();
    coordinator.copy(Interpretation::columns);
    coordinator.copy(Interpretation::anywhere);
    f.drain();
    REQUIRE_FALSE(f.reads.empty());
    for (const auto& read : f.reads) {
        CHECK(read.characters() == beebium::SCREEN_TEXT_CHARACTERS_DISPLAYED);
    }
}

TEST_CASE("A copy that finds nothing copies nothing", "[selection][copy]") {
    Fakes f;
    f.reading.text = "";
    SelectionCoordinator coordinator(f.services());
    coordinator.copy(Interpretation::rows);
    f.drain();
    CHECK(f.clipboard.empty());
    CHECK(f.nothing_to_copy == 1);
    // Nor do blank rows alone.
    f.reading.text = "\n\n\n";
    coordinator.copy(Interpretation::rows);
    f.drain();
    CHECK(f.clipboard.empty());
    CHECK(f.nothing_to_copy == 2);
    CHECK(f.failures.empty());
}

TEST_CASE("A copy whose read fails says so in the server's words", "[selection][copy]") {
    Fakes f;
    f.read_failure = ScreenReadFailure{false, "the display is not readable"};
    SelectionCoordinator coordinator(f.services());
    coordinator.copy(Interpretation::rows);
    f.drain();
    CHECK(f.failures == std::vector<std::string>{"the display is not readable"});
    CHECK(f.nothing_to_copy == 0);
}

TEST_CASE("Copy Text from Graphics with no selection reads the whole screen anywhere",
          "[selection][copy]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.copy(Interpretation::anywhere);
    coordinator.copy(Interpretation::columns);
    f.drain();
    REQUIRE(f.reads.size() == 2);
    CHECK(f.reads[0].search() == beebium::SCREEN_TEXT_SEARCH_ANYWHERE);
    CHECK_FALSE(f.reads[0].has_region());
    CHECK(f.reads[1].search() == beebium::SCREEN_TEXT_SEARCH_ALIGNED);
    CHECK_FALSE(f.reads[1].has_region());
}

TEST_CASE("A stale highlight never paints over a newer one", "[selection][overlay]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::rows, filling());
    f.drain();
    TextRun stale;
    stale.text = "OLD";
    stale.bounds = {0, 0, 24, 8};
    f.reading.runs = {stale};
    f.reading.text = "OLD";
    coordinator.update(at(0.5, 0.5));  // its answer is held back
    auto stale_answer = std::move(f.pending_reads.back());
    f.pending_reads.pop_back();
    f.reading.runs.clear();
    coordinator.update(at(0.6, 0.6));
    f.drain();
    CHECK(coordinator.overlay().text.empty());
    stale_answer();
    CHECK(coordinator.overlay().text.empty());
}

TEST_CASE("Changing the interpretation re-reads the held still", "[selection][overlay]") {
    Fakes f;
    SelectionCoordinator coordinator(f.services());
    coordinator.begin(at(0.2, 0.2), Interpretation::rows, filling());
    f.drain();
    f.reads.clear();
    coordinator.set_interpretation(Interpretation::anywhere);
    CHECK(coordinator.interpretation() == Interpretation::anywhere);
    REQUIRE(f.reads.size() == 1);
    CHECK(f.reads[0].search() == beebium::SCREEN_TEXT_SEARCH_ANYWHERE);
    coordinator.set_interpretation(Interpretation::anywhere);
    CHECK(f.reads.size() == 1);
}

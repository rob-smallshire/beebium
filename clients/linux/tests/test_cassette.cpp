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

// The cassette deck (client specification 9.0): its state from the server's
// status stream, the keys and inserts with classified outcomes, and blank
// tapes (9.0.1).

#include "app/SessionCassette.hpp"

#include "FakeBeebiumServer.hpp"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

std::shared_ptr<grpc::Channel> channel_to(const FakeServer& server) {
    return grpc::CreateChannel(server.endpoint().toStdString(), grpc::InsecureChannelCredentials());
}

struct Connected {
    FakeServer server;
    SessionCassette cassette;
    explicit Connected(bool has_interface = true) {
        server.cassette.has_interface = has_interface;
        cassette.start(channel_to(server));
        REQUIRE(wait_until([&] { return cassette.deck().known; }));
    }
    ~Connected() { cassette.stop(); }
    const DeckState& deck() const { return cassette.deck(); }
    // A key pressed, and its outcome once the server has answered.
    CassetteOutcome press(CassetteKey key) {
        std::optional<CassetteOutcome> outcome;
        auto connection = QObject::connect(&cassette, &SessionCassette::key_finished,
                                           [&](CassetteKey, const CassetteOutcome& o) { outcome = o; });
        cassette.press(key);
        REQUIRE(wait_until([&] { return outcome.has_value(); }));
        QObject::disconnect(connection);
        return *outcome;
    }
    void insert(const QString& path) {
        std::optional<CassetteOutcome> outcome;
        auto connection = QObject::connect(
            &cassette, &SessionCassette::insert_finished,
            [&](const QString&, const CassetteOutcome& o) { outcome = o; });
        cassette.insert(path);
        REQUIRE(wait_until([&] { return outcome.has_value(); }));
        QObject::disconnect(connection);
        REQUIRE(outcome->ok);
        REQUIRE(wait_until([&] { return deck().tape_inserted; }));
    }
};

beebium::DeckStatus waiting_tape() {
    beebium::DeckStatus status;
    status.set_has_cassette_interface(true);
    status.set_tape_inserted(true);
    auto* tape = status.mutable_tape();
    tape->set_name("Elite.uef");
    tape->set_url("file:///home/me/tapes/Elite.uef");
    tape->set_format("UEF");
    tape->set_title("Elite");
    tape->set_origin("UEF tools 0.1");
    tape->set_target_machine("BBC Model B");
    tape->set_duration_seconds(912.5);
    tape->set_recordable(true);
    status.set_transport(beebium::TAPE_TRANSPORT_PLAYING);
    status.set_position_seconds(65.9);
    status.set_motor_on(false);
    return status;
}

}  // namespace

TEST_CASE("The deck's state is the status as reported, the tape described", "[cassette]") {
    const DeckState deck = deck_state_from(waiting_tape());
    CHECK(deck.known);
    CHECK(deck.has_interface);
    CHECK(deck.tape_inserted);
    CHECK(deck.tape.name == QStringLiteral("Elite.uef"));
    CHECK(deck.tape.format == QStringLiteral("UEF"));
    CHECK(deck.tape.title == QStringLiteral("Elite"));
    CHECK(deck.tape.origin == QStringLiteral("UEF tools 0.1"));
    CHECK(deck.tape.target_machine == QStringLiteral("BBC Model B"));
    CHECK(deck.tape.duration_seconds == 912.5);
    CHECK(deck.tape.recordable);
    CHECK(deck.transport == DeckState::Transport::playing);
    CHECK(deck.position_seconds == 65.9);
    CHECK_FALSE(deck.motor_on);
}

TEST_CASE("An empty deck's status carries no tape, whatever the message holds", "[cassette]") {
    beebium::DeckStatus status = waiting_tape();
    status.set_tape_inserted(false);
    const DeckState deck = deck_state_from(status);
    CHECK_FALSE(deck.tape_inserted);
    CHECK(deck.tape == TapeInfo{});
}

TEST_CASE("The tape counter reads minutes and whole seconds", "[cassette]") {
    CHECK(tape_counter_text(0.0) == QStringLiteral("0:00"));
    CHECK(tape_counter_text(5.99) == QStringLiteral("0:05"));
    CHECK(tape_counter_text(65.9) == QStringLiteral("1:05"));
    CHECK(tape_counter_text(725.0) == QStringLiteral("12:05"));
    CHECK(tape_counter_text(4530.0) == QStringLiteral("75:30"));
    CHECK(tape_counter_text(-1.0) == QStringLiteral("0:00"));
}

TEST_CASE("The deck reads its transport and its relay together", "[cassette]") {
    DeckState deck = deck_state_from(waiting_tape());
    // The ordinary state of a tape waiting for the machine, not a fault.
    CHECK(deck_state_text(deck) == QStringLiteral("Playing, motor off"));
    deck.motor_on = true;
    CHECK(deck_state_text(deck) == QStringLiteral("Playing, motor on"));
    deck.transport = DeckState::Transport::recording;
    deck.motor_on = false;
    CHECK(deck_state_text(deck) == QStringLiteral("Recording, motor off"));
    deck.transport = DeckState::Transport::stopped;
    CHECK(deck_state_text(deck) == QStringLiteral("Stopped, motor off"));
    deck.transport = DeckState::Transport::rewinding;
    CHECK(deck_state_text(deck) == QStringLiteral("Rewinding, motor off"));
    deck.transport = DeckState::Transport::fast_forwarding;
    deck.motor_on = true;
    CHECK(deck_state_text(deck) == QStringLiteral("Fast forwarding, motor on"));
}

TEST_CASE("The deck is the stream's first status, asked for at the counter's rate", "[cassette]") {
    Connected c;
    CHECK(c.deck().has_interface);
    CHECK_FALSE(c.deck().tape_inserted);
    CHECK(c.deck().transport == DeckState::Transport::stopped);
    std::lock_guard lock(c.server.cassette.calls_mutex);
    CHECK(c.server.cassette.min_interval_ms == 250);
}

TEST_CASE("A model without a cassette interface has no deck: a state, not an error",
          "[cassette]") {
    Connected c(false);
    CHECK_FALSE(c.deck().has_interface);
}

TEST_CASE("An insert sends the encoded URL and leaves the deck stopped at the start",
          "[cassette][insert]") {
    Connected c;
    c.insert(QStringLiteral("/tmp/My Tapes/Side A.uef"));
    {
        std::lock_guard lock(c.server.cassette.calls_mutex);
        REQUIRE(c.server.cassette.inserted_urls.size() == 1);
        CHECK(c.server.cassette.inserted_urls[0] == "file:///tmp/My%20Tapes/Side%20A.uef");
    }
    CHECK(c.deck().tape.name == QStringLiteral("Side A.uef"));
    CHECK(c.deck().transport == DeckState::Transport::stopped);
    CHECK(c.deck().position_seconds == 0.0);
    // Play is the user's, after the insert, never part of it.
    CHECK(c.server.cassette.pressed().empty());
}

TEST_CASE("The relay and the position follow the stream while the tape waits and moves",
          "[cassette]") {
    Connected c;
    c.insert(QStringLiteral("/tmp/Elite.uef"));
    CHECK(c.press(CassetteKey::play).ok);
    REQUIRE(wait_until([&] { return c.deck().transport == DeckState::Transport::playing; }));
    CHECK(deck_state_text(c.deck()) == QStringLiteral("Playing, motor off"));
    c.server.cassette.set_motor(true);
    REQUIRE(wait_until([&] { return c.deck().motor_on; }));
    c.server.cassette.set_position(42.0);
    REQUIRE(wait_until([&] { return c.deck().position_seconds == 42.0; }));
}

TEST_CASE("The keys go to the server, which changes the transport", "[cassette][keys]") {
    Connected c;
    c.insert(QStringLiteral("/tmp/Elite.uef"));
    c.server.cassette.set_position(100.0);
    REQUIRE(wait_until([&] { return c.deck().position_seconds == 100.0; }));
    CHECK(c.press(CassetteKey::rewind).ok);
    REQUIRE(wait_until([&] { return c.deck().transport == DeckState::Transport::rewinding; }));
    CHECK(c.press(CassetteKey::fast_forward).ok);
    REQUIRE(wait_until([&] { return c.deck().transport == DeckState::Transport::fast_forwarding; }));
    CHECK(c.press(CassetteKey::stop).ok);
    REQUIRE(wait_until([&] { return c.deck().transport == DeckState::Transport::stopped; }));
    CHECK(c.press(CassetteKey::record).ok);
    REQUIRE(wait_until([&] { return c.deck().transport == DeckState::Transport::recording; }));
    std::optional<CassetteOutcome> sought;
    QObject::connect(&c.cassette, &SessionCassette::key_finished,
                     [&](CassetteKey key, const CassetteOutcome& o) {
                         if (key == CassetteKey::seek) {
                             sought = o;
                         }
                     });
    // Back to the beginning is a seek to 0, which ends a recording.
    c.cassette.seek(0.0);
    REQUIRE(wait_until([&] { return sought.has_value(); }));
    CHECK(sought->ok);
    REQUIRE(wait_until([&] {
        return c.deck().position_seconds == 0.0 && c.deck().transport == DeckState::Transport::stopped;
    }));
    CHECK(c.press(CassetteKey::eject).ok);
    REQUIRE(wait_until([&] { return !c.deck().tape_inserted; }));
    CHECK(c.server.cassette.pressed() ==
          std::vector<std::string>{"rewind", "fast forward", "stop", "record", "seek 0", "eject"});
}

TEST_CASE("A refused key comes back with the server's sentence and its kind",
          "[cassette][errors]") {
    Connected c;
    const CassetteOutcome play = c.press(CassetteKey::play);
    CHECK_FALSE(play.ok);
    CHECK(play.reached);
    CHECK(play.kind == beebium::CASSETTE_ERROR_KIND_DECK_EMPTY);
    CHECK(play.message == QStringLiteral("There is no tape in the cassette deck."));
}

TEST_CASE("A CSW cannot be recorded on, and says so", "[cassette][errors]") {
    Connected c;
    c.insert(QStringLiteral("/tmp/Repton.csw"));
    CHECK(c.deck().tape.format == QStringLiteral("CSW"));
    CHECK_FALSE(c.deck().tape.recordable);
    const CassetteOutcome record = c.press(CassetteKey::record);
    CHECK_FALSE(record.ok);
    CHECK(record.kind == beebium::CASSETTE_ERROR_KIND_READ_ONLY);
    CHECK(record.message ==
          QStringLiteral("The tape Repton.csw is a CSW image, and Beebium records only on UEF tapes."));
    CHECK(c.deck().transport == DeckState::Transport::stopped);
}

TEST_CASE("The blank tapes are the server's templates; one is made with its extension",
          "[cassette][blank]") {
    Connected c;
    REQUIRE(wait_until([&] { return c.cassette.blank_tapes().size() == 2; }));
    const BlankTapeTemplate& c15 = c.cassette.blank_tapes().front();
    CHECK(c15.label == QStringLiteral("Blank C15 (15 minutes)"));
    CHECK(c15.extension() == QStringLiteral(".uef"));
    CHECK(c15.duration_seconds == 450.0);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    std::optional<CassetteOutcome> outcome;
    QObject::connect(&c.cassette, &SessionCassette::create_finished,
                     [&](const QString&, const CassetteOutcome& o) { outcome = o; });
    c.cassette.create_tape(c15.label, dir.filePath(QStringLiteral("Mine")));
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK(outcome->ok);
    CHECK(outcome->created_path == dir.filePath(QStringLiteral("Mine.uef")));
    CHECK(QFileInfo::exists(outcome->created_path));
}

TEST_CASE("Stopping the session forgets the deck", "[cassette]") {
    Connected c;
    c.insert(QStringLiteral("/tmp/Elite.uef"));
    bool told = false;
    QObject::connect(&c.cassette, &SessionCassette::deck_changed, [&] { told = true; });
    c.cassette.stop();
    CHECK(told);
    CHECK_FALSE(c.deck().known);
    CHECK_FALSE(c.deck().tape_inserted);
}

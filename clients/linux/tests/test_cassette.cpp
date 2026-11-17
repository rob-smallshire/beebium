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

#include "app/CassetteDeckRow.hpp"
#include "app/ClientSettings.hpp"
#include "app/SessionCassette.hpp"
#include "app/SessionDiscs.hpp"
#include "app/StorageSidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QToolButton>

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

TEST_CASE("The deck reads its transport, naming the relay while it holds the tape back",
          "[cassette]") {
    DeckState deck = deck_state_from(waiting_tape());
    // The ordinary state of a tape waiting for the machine, not a fault.
    CHECK(deck_state_text(deck) == QStringLiteral("Playing, motor off"));
    deck.motor_on = true;
    CHECK(deck_state_text(deck) == QStringLiteral("Playing"));
    deck.transport = DeckState::Transport::recording;
    CHECK(deck_state_text(deck) == QStringLiteral("Recording"));
    deck.motor_on = false;
    CHECK(deck_state_text(deck) == QStringLiteral("Recording, motor off"));
    deck.transport = DeckState::Transport::stopped;
    CHECK(deck_state_text(deck) == QStringLiteral("Stopped"));
    // A wind runs whether or not the relay is closed.
    deck.transport = DeckState::Transport::rewinding;
    CHECK(deck_state_text(deck) == QStringLiteral("Rewinding"));
    deck.transport = DeckState::Transport::fast_forwarding;
    deck.motor_on = true;
    CHECK(deck_state_text(deck) == QStringLiteral("Fast forwarding"));
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

namespace {

// A deck row on a connected session, its settings in a scratch file.
struct Deck {
    FakeServer server;
    SessionCassette cassette;
    QTemporaryDir dir;
    ClientSettings settings{dir.filePath(QStringLiteral("Beebium.conf"))};
    bool local = true;
    CassetteDeckRow row{cassette, &settings, [this] { return local; }};
    Deck() {
        REQUIRE(dir.isValid());
        cassette.start(channel_to(server));
        REQUIRE(wait_until([&] { return cassette.deck().known; }));
    }
    ~Deck() { cassette.stop(); }
    // A tape as a preset puts it in the deck, stopped at the start.
    void load(const char* url = "file:///home/me/tapes/Elite.uef") {
        server.cassette.load(url);
        REQUIRE(wait_until([&] { return cassette.deck().tape_inserted; }));
    }
    // A real file to insert.
    QString tape_file(const QString& name) {
        const QString path = dir.filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        return path;
    }
    std::vector<std::string> pressed() { return server.cassette.pressed(); }
};

QAction* action_named(QMenu* menu, const QString& text) {
    for (QAction* action : menu->actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("An empty deck offers Browse where the server is local, and keys only with a tape",
          "[cassette][deck]") {
    Deck d;
    CHECK(d.row.name_text() == QStringLiteral("Empty - Drop tape image here"));
    CHECK(d.row.browse_button()->isEnabled());
    for (CassetteKey key : {CassetteKey::rewind, CassetteKey::seek, CassetteKey::play,
                            CassetteKey::stop, CassetteKey::record, CassetteKey::fast_forward,
                            CassetteKey::eject}) {
        CHECK_FALSE(d.row.key(key)->isEnabled());
    }
    CHECK(d.row.state_text().isEmpty());
    CHECK(d.row.counter_text().isEmpty());
    d.local = false;
    d.row.refresh();
    d.row.clear_message();
    CHECK(d.row.name_text() == QStringLiteral("Empty - server is on another host"));
    CHECK_FALSE(d.row.browse_button()->isEnabled());
}

TEST_CASE("A loaded tape shows its name, what it is, its state, the counter and its length",
          "[cassette][deck]") {
    Deck d;
    d.load();
    CHECK(d.row.name_text() == QStringLiteral("Elite.uef"));
    CHECK(d.row.metadata_text() == QStringLiteral("UEF - Side A - BBC Model B"));
    CHECK(d.row.state_text() == QStringLiteral("Stopped"));
    CHECK(d.row.counter_text() == QStringLiteral("0:00"));
    CHECK(d.row.duration_text() == QStringLiteral("10:00"));
    CHECK(d.row.slider()->isVisibleTo(&d.row));
    CHECK(d.row.slider()->maximum() == 6000);
    CHECK(d.row.key(CassetteKey::play)->isEnabled());
    CHECK_FALSE(d.row.key(CassetteKey::stop)->isEnabled());
    CHECK_FALSE(d.row.key(CassetteKey::seek)->isEnabled());  // already at the beginning
    CHECK(d.row.key(CassetteKey::record)->isEnabled());
    CHECK(d.row.key(CassetteKey::eject)->isEnabled());
    CHECK_FALSE(d.row.browse_button()->isEnabled());
}

TEST_CASE("A tape of unknown length has the counter alone, with no slider", "[cassette][deck]") {
    Deck d;
    d.server.cassette.duration_seconds = 0.0;
    d.load();
    CHECK(d.row.counter_text() == QStringLiteral("0:00"));
    CHECK(d.row.duration_text().isEmpty());
    CHECK_FALSE(d.row.slider()->isVisibleTo(&d.row));
}

TEST_CASE("Play with the motor off reads as a tape waiting; the relay shows as a dot",
          "[cassette][deck]") {
    Deck d;
    d.load();
    d.row.key(CassetteKey::play)->click();
    REQUIRE(wait_until([&] { return d.row.state_text() == QStringLiteral("Playing, motor off"); }));
    CHECK_FALSE(d.row.motor_shown());
    CHECK_FALSE(d.row.key(CassetteKey::play)->isEnabled());
    CHECK(d.row.key(CassetteKey::stop)->isEnabled());
    d.server.cassette.set_motor(true);
    REQUIRE(wait_until([&] { return d.row.motor_shown(); }));
    CHECK(d.row.state_text() == QStringLiteral("Playing"));
    d.server.cassette.set_position(125.4);
    REQUIRE(wait_until([&] { return d.row.counter_text() == QStringLiteral("2:05"); }));
    CHECK(d.row.slider()->value() == 1254);
    // Back to the beginning: a seek to 0, and the deck plays on.
    REQUIRE(d.row.key(CassetteKey::seek)->isEnabled());
    d.row.key(CassetteKey::seek)->click();
    REQUIRE(wait_until([&] { return d.row.counter_text() == QStringLiteral("0:00"); }));
    CHECK(d.row.state_text() == QStringLiteral("Playing"));
    CHECK(d.pressed() == std::vector<std::string>{"play", "seek 0"});
}

TEST_CASE("The slider seeks once, when let go, the counter following the drag meanwhile",
          "[cassette][deck][scrub]") {
    Deck d;
    d.load();
    d.row.key(CassetteKey::play)->click();
    REQUIRE(wait_until([&] { return d.row.state_text() == QStringLiteral("Playing, motor off"); }));
    QSlider* slider = d.row.slider();
    slider->setSliderDown(true);
    slider->setSliderPosition(3000);
    CHECK(d.row.counter_text() == QStringLiteral("5:00"));
    // The tape moving meanwhile does not take the slider from the hand.
    d.server.cassette.set_position(10.0);
    QCoreApplication::processEvents();
    REQUIRE(wait_until([&] { return d.cassette.deck().position_seconds == 10.0; }));
    CHECK(slider->sliderPosition() == 3000);
    CHECK(d.row.counter_text() == QStringLiteral("5:00"));
    CHECK(d.pressed() == std::vector<std::string>{"play"});
    slider->setSliderDown(false);
    REQUIRE(wait_until([&] { return d.pressed().size() == 2; }));
    CHECK(d.pressed().back() == "seek 300");
    REQUIRE(wait_until([&] { return d.row.counter_text() == QStringLiteral("5:00"); }));
    // A drag made while the deck plays: it plays on from there.
    CHECK(d.row.state_text() == QStringLiteral("Playing, motor off"));
}

TEST_CASE("Record is red and disabled with its reason for a tape Beebium only reads",
          "[cassette][deck][record]") {
    Deck d;
    d.load("file:///home/me/tapes/Repton.csw");
    QToolButton* record = d.row.key(CassetteKey::record);
    CHECK_FALSE(record->isEnabled());
    CHECK(record->toolTip() ==
          QStringLiteral("This tape is a CSW image, and Beebium records only on UEF tapes"));
    CHECK(d.row.metadata_text() == QStringLiteral("CSW - CSW tools"));
}

TEST_CASE("A refused request shows the server's sentence in full, its kind choosing the icon",
          "[cassette][deck][errors]") {
    Deck d;
    d.load("file:///home/me/tapes/Repton.csw");
    d.cassette.press(CassetteKey::record);
    REQUIRE(wait_until([&] { return !d.row.message_text().isEmpty(); }));
    CHECK(d.row.message_text() ==
          QStringLiteral("The tape Repton.csw is a CSW image, and Beebium records only on UEF tapes."));
    CHECK(d.row.message().kind == CassetteMessage::Kind::failure);
    CHECK(d.row.message().icon == QStyle::SP_MessageBoxInformation);
    CHECK(d.row.dismiss_button()->isVisibleTo(&d.row));
    CHECK_FALSE(d.row.eject_offer()->isVisibleTo(&d.row));
    // It stays until dismissed.
    d.row.dismiss_button()->click();
    CHECK(d.row.message_text().isEmpty());
}

TEST_CASE("The icon is the kind's, and an unknown kind still shows its sentence",
          "[cassette][errors]") {
    CHECK(cassette_error_icon(beebium::CASSETTE_ERROR_KIND_DECK_EMPTY) ==
          QStyle::SP_MessageBoxInformation);
    CHECK(cassette_error_icon(beebium::CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT) ==
          QStyle::SP_MessageBoxWarning);
    CHECK(cassette_error_icon(99) == QStyle::SP_MessageBoxWarning);
    CHECK(create_tape_error_sentence(beebium::CREATE_TAPE_ERROR_KIND_WRONG_EXTENSION) ==
          QStringLiteral("That name must end in .uef for a tape."));
    CHECK(create_tape_error_sentence(99) == QStringLiteral("The tape image could not be created."));
}

TEST_CASE("An unsupported file is the server's to refuse, and its sentence is shown",
          "[cassette][deck][errors]") {
    Deck d;
    {
        std::lock_guard lock(d.server.cassette.calls_mutex);
        d.server.cassette.fail_next_insert = std::make_pair(
            beebium::CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT,
            std::string("The file notes.uef is neither a UEF nor a CSW tape image."));
    }
    // Accepted here whatever its content: the client never judges the file.
    d.row.drop_files({d.tape_file(QStringLiteral("notes.uef"))});
    REQUIRE(wait_until([&] { return !d.row.message_text().isEmpty(); }));
    CHECK(d.row.message_text() ==
          QStringLiteral("The file notes.uef is neither a UEF nor a CSW tape image."));
    CHECK(d.row.message().icon == QStyle::SP_MessageBoxWarning);
    // A failed insert is not recent.
    CHECK(d.settings.recent_tapes().isEmpty());
}

TEST_CASE("An insert onto an occupied deck is refused with an Eject offer", "[cassette][deck][errors]") {
    Deck d;
    d.load();
    d.cassette.insert(QStringLiteral("/home/me/tapes/Other.uef"));
    REQUIRE(wait_until([&] { return d.row.eject_offer()->isVisibleTo(&d.row); }));
    CHECK(d.row.message().offer_eject);
    CHECK(d.row.message().icon == QStyle::SP_MessageBoxInformation);
    d.row.eject_offer()->click();
    REQUIRE(wait_until([&] { return !d.cassette.deck().tape_inserted; }));
    // The eject ends what the failure said.
    CHECK(d.row.message_text().isEmpty());
    CHECK(d.row.name_text() == QStringLiteral("Empty - Drop tape image here"));
}

TEST_CASE("A drop is refused for a remote server, then an occupied deck, then many files",
          "[cassette][deck][drop]") {
    Deck d;
    d.local = false;
    CHECK(d.row.drop_refusal() == QStringLiteral("Server is on another host"));
    d.local = true;
    CHECK(d.row.drop_refusal().isEmpty());
    d.row.drop_files({QStringLiteral("/a.uef"), QStringLiteral("/b.uef")});
    CHECK(d.row.message_text() == QStringLiteral("Drop a single tape image"));
    CHECK(d.row.message().kind == CassetteMessage::Kind::notice);
    d.load();
    CHECK(d.row.drop_refusal() == QStringLiteral("Eject tape first"));
    {
        std::lock_guard lock(d.server.cassette.calls_mutex);
        CHECK(d.server.cassette.inserted_urls.empty());
    }
}

TEST_CASE("A taken insert is recent, offered again from the menu with the blank tapes",
          "[cassette][deck][recent]") {
    Deck d;
    const QString path = d.tape_file(QStringLiteral("Side B.uef"));
    d.row.drop_files({path});
    REQUIRE(wait_until([&] { return d.cassette.deck().tape_inserted; }));
    REQUIRE(wait_until([&] { return !d.settings.recent_tapes().isEmpty(); }));
    CHECK(d.settings.recent_tapes().front() == path);
    CHECK(d.settings.recent_discs().isEmpty());
    REQUIRE(wait_until([&] { return d.cassette.blank_tapes().size() == 2; }));
    Q_EMIT d.row.browse_menu()->aboutToShow();
    QAction* recent = action_named(d.row.browse_menu(), QStringLiteral("Recent"));
    REQUIRE(recent);
    REQUIRE(recent->menu());
    QAction* entry = action_named(recent->menu(), QStringLiteral("Side B.uef"));
    REQUIRE(entry);
    CHECK(entry->isEnabled());
    CHECK(action_named(recent->menu(), QStringLiteral("Clear Recent Tapes")));
    QAction* blank = action_named(d.row.browse_menu(), QStringLiteral("New Blank Tape"));
    REQUIRE(blank);
    REQUIRE(blank->menu());
    CHECK(blank->isEnabled());
    QStringList labels;
    for (QAction* action : blank->menu()->actions()) {
        labels << action->text();
    }
    CHECK(labels == QStringList{QStringLiteral("Blank C15 (15 minutes)"),
                                QStringLiteral("Blank C30 (30 minutes)")});
}

TEST_CASE("A blank tape is made, then inserted stopped at the start, then recent",
          "[cassette][deck][blank]") {
    Deck d;
    d.cassette.create_tape(QStringLiteral("Blank C30 (30 minutes)"), d.dir.filePath(QStringLiteral("Mine")));
    REQUIRE(wait_until([&] { return d.cassette.deck().tape_inserted; }));
    CHECK(d.cassette.deck().tape.name == QStringLiteral("Mine.uef"));
    CHECK(d.cassette.deck().transport == DeckState::Transport::stopped);
    CHECK(d.cassette.deck().position_seconds == 0.0);
    REQUIRE(wait_until([&] { return !d.settings.recent_tapes().isEmpty(); }));
    CHECK(d.settings.recent_tapes().front() == d.dir.filePath(QStringLiteral("Mine.uef")));
    // Recording stays the user's choice.
    CHECK(d.pressed().empty());
}

TEST_CASE("A blank tape the server refuses leads with the kind's sentence, its words beneath",
          "[cassette][deck][blank]") {
    Deck d;
    d.cassette.create_tape(QStringLiteral("Blank C30 (30 minutes)"), d.dir.filePath(QStringLiteral("Mine.ssd")));
    REQUIRE(wait_until([&] { return !d.row.message_text().isEmpty(); }));
    CHECK(d.row.message_text() == QStringLiteral("That name must end in .uef for a tape."));
    CHECK(d.row.message().detail == QStringLiteral("a blank tape must end in .uef"));
    CHECK_FALSE(d.cassette.deck().tape_inserted);
}

TEST_CASE("The tape's menu copies its path and opens its folder where it is here",
          "[cassette][deck]") {
    Deck d;
    CHECK(d.row.make_path_menu() == nullptr);
    const QString path = d.tape_file(QStringLiteral("Elite.uef"));
    d.row.drop_files({path});
    REQUIRE(wait_until([&] { return d.cassette.deck().tape_inserted; }));
    std::unique_ptr<QMenu> menu(d.row.make_path_menu());
    REQUIRE(menu);
    QAction* copy = action_named(menu.get(), QStringLiteral("Copy Path"));
    REQUIRE(copy);
    copy->trigger();
    CHECK(QApplication::clipboard()->text() == path);
    QAction* reveal = action_named(menu.get(), QStringLiteral("Open Containing Folder"));
    REQUIRE(reveal);
    CHECK(reveal->isEnabled());
}

TEST_CASE("Nothing on the deck takes the keyboard from the machine", "[cassette][deck][focus]") {
    Deck d;
    d.load();
    for (QWidget* widget : d.row.findChildren<QWidget*>()) {
        INFO(widget->metaObject()->className());
        CHECK(widget->focusPolicy() == Qt::NoFocus);
    }
}

namespace {

struct Storage {
    FakeServer server;
    SessionDiscs discs;
    SessionCassette cassette;
    QTemporaryDir dir;
    ClientSettings settings{dir.filePath(QStringLiteral("Beebium.conf"))};
    std::unique_ptr<StorageSidebar> sidebar;
    Storage(bool controller, bool cassette_interface) {
        server.disc.has_controller = controller;
        server.cassette.has_interface = cassette_interface;
        sidebar = std::make_unique<StorageSidebar>(discs, &settings, [] { return true; });
        sidebar->set_cassette(cassette);
        sidebar->resize(280, 600);
        sidebar->show();
        discs.start(channel_to(server));
        cassette.start(channel_to(server));
        REQUIRE(wait_until([&] { return discs.loaded() && cassette.deck().known; }));
        QCoreApplication::processEvents();
    }
    ~Storage() {
        sidebar.reset();
        cassette.stop();
        discs.stop();
    }
};

}  // namespace

TEST_CASE("The cassette deck sits above the floppy drives", "[cassette][sidebar]") {
    Storage s(true, true);
    REQUIRE(s.sidebar->deck_shown());
    CassetteDeckRow* deck = s.sidebar->cassette_deck();
    REQUIRE(deck);
    REQUIRE(wait_until([&] { return deck->isVisible() && !s.sidebar->rows().empty(); }));
    DriveRow* drive = s.sidebar->rows().front();
    REQUIRE(wait_until([&] { return drive->isVisible(); }));
    CHECK(deck->mapTo(s.sidebar.get(), QPoint(0, 0)).y() <
          drive->mapTo(s.sidebar.get(), QPoint(0, 0)).y());
    CHECK(s.sidebar->status_text().isEmpty());
}

TEST_CASE("A model without a cassette interface has no deck, not a disabled one",
          "[cassette][sidebar]") {
    Storage s(true, false);
    CHECK_FALSE(s.sidebar->deck_shown());
    CHECK_FALSE(s.sidebar->cassette_deck()->isVisibleTo(s.sidebar.get()));
    CHECK(s.sidebar->rows().size() == 2);
}

TEST_CASE("A deck without a disc controller is still storage; neither says there is none",
          "[cassette][sidebar]") {
    {
        Storage s(false, true);
        CHECK(s.sidebar->status_text().isEmpty());
        CHECK(s.sidebar->cassette_deck()->isVisibleTo(s.sidebar.get()));
    }
    {
        Storage s(false, false);
        CHECK(s.sidebar->status_text() == QStringLiteral("No Storage Devices"));
    }
}

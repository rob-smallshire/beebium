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

// The floppy drives (client specification 9): the server's snapshot and
// events, and inserts, ejects and blank discs with classified outcomes.

#include "app/ClientSettings.hpp"
#include "app/SessionDiscs.hpp"
#include "app/StorageSidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QFileInfo>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

std::shared_ptr<grpc::Channel> channel_to(const FakeServer& server) {
    return grpc::CreateChannel(server.endpoint().toStdString(), grpc::InsecureChannelCredentials());
}

struct Connected {
    FakeServer server;
    SessionDiscs discs;
    explicit Connected(bool controller = true) {
        server.disc.has_controller = controller;
        discs.start(channel_to(server));
        REQUIRE(wait_until([&] { return discs.loaded(); }));
        REQUIRE(wait_until([&] { return server.disc.subscriptions == 1; }));
    }
    ~Connected() { discs.stop(); }
    DriveInfo::State state(int number) const { return discs.drive(number)->state; }
};

}  // namespace

TEST_CASE("A disc path goes as an encoded file URL, and comes back from either form",
          "[storage][url]") {
    CHECK(disc_url_for_path(QStringLiteral("/home/u/My Discs/Elite 2.ssd")) ==
          QStringLiteral("file:///home/u/My%20Discs/Elite%202.ssd"));
    CHECK(path_for_disc_url(QStringLiteral("file:///home/u/My%20Discs/Elite%202.ssd")) ==
          QStringLiteral("/home/u/My Discs/Elite 2.ssd"));
    // Mounted by a preset or the command line: a bare path, spaces as they are.
    CHECK(path_for_disc_url(QStringLiteral("/opt/discs/Welcome Disc.ssd")) ==
          QStringLiteral("/opt/discs/Welcome Disc.ssd"));
}

TEST_CASE("The drives are the server's snapshot, then its events", "[storage]") {
    Connected c;
    CHECK(c.discs.has_controller());
    CHECK(c.discs.controller_type() == QStringLiteral("WD1770"));
    REQUIRE(c.discs.drives().size() == 2);
    CHECK(c.state(0) == DriveInfo::State::empty);
    c.server.disc.announce(beebium::DISC_EVENT_MOTOR_ON, 1);
    REQUIRE(wait_until([&] { return c.discs.drive(1)->motor_on; }));
    c.server.disc.announce(beebium::DISC_EVENT_MOTOR_OFF, 1);
    REQUIRE(wait_until([&] { return !c.discs.drive(1)->motor_on; }));
}

TEST_CASE("No disc controller is a state, not an error", "[storage]") {
    Connected c(false);
    CHECK_FALSE(c.discs.has_controller());
    CHECK(c.discs.drives().empty());
}

TEST_CASE("An insert sends the encoded URL and the drive loads from the event",
          "[storage][insert]") {
    Connected c;
    std::optional<DiscOutcome> outcome;
    QObject::connect(&c.discs, &SessionDiscs::insert_finished,
                     [&](int, const QString&, const DiscOutcome& o) { outcome = o; });
    c.discs.insert(0, QStringLiteral("/tmp/With Space.ssd"));
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK(outcome->ok);
    {
        std::lock_guard lock(c.server.disc.calls_mutex);
        REQUIRE(c.server.disc.inserted_urls.size() == 1);
        CHECK(c.server.disc.inserted_urls[0] == "file:///tmp/With%20Space.ssd");
    }
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::loaded; }));
    CHECK(c.discs.drive(0)->format == QStringLiteral("SSD"));
    CHECK(c.discs.drive(0)->sides == 1);
}

TEST_CASE("A failed insert comes back classified, in the server's own words",
          "[storage][insert]") {
    Connected c;
    {
        std::lock_guard lock(c.server.disc.calls_mutex);
        c.server.disc.fail_next_insert = std::make_pair(
            beebium::DISC_ERROR_KIND_UNRECOGNISED, std::string("no handler for this image"));
    }
    std::optional<DiscOutcome> outcome;
    QObject::connect(&c.discs, &SessionDiscs::insert_finished,
                     [&](int, const QString&, const DiscOutcome& o) { outcome = o; });
    c.discs.insert(1, QStringLiteral("/tmp/notes.txt"));
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK_FALSE(outcome->ok);
    CHECK(outcome->kind == beebium::DISC_ERROR_KIND_UNRECOGNISED);
    CHECK(outcome->message == QStringLiteral("no handler for this image"));
    CHECK(c.state(1) == DriveInfo::State::empty);
}

TEST_CASE("A safe eject waits on a busy drive until cancelled or forced", "[storage][eject]") {
    Connected c;
    c.discs.insert(0, QStringLiteral("/tmp/a.ssd"));
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::loaded; }));
    {
        std::lock_guard lock(c.server.disc.calls_mutex);
        c.server.disc.drive_busy = true;
    }
    c.discs.eject(0, false);
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::ejecting; }));
    c.discs.cancel_eject(0);
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::loaded; }));
    c.discs.eject(0, false);
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::ejecting; }));
    c.discs.eject(0, true);  // Force Eject
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::empty; }));
    std::lock_guard lock(c.server.disc.calls_mutex);
    CHECK(c.server.disc.immediate_ejects == 1);
}

TEST_CASE("A safe eject of a quiet drive completes by itself", "[storage][eject]") {
    Connected c;
    c.discs.insert(0, QStringLiteral("/tmp/a.ssd"));
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::loaded; }));
    c.discs.eject(0, false);
    REQUIRE(wait_until([&] { return c.state(0) == DriveInfo::State::empty; }));
    CHECK(c.discs.drive(0)->disc_name.isEmpty());
}

TEST_CASE("The blank discs are the server's templates; one is made with its extension",
          "[storage][blank]") {
    Connected c;
    REQUIRE(wait_until([&] { return c.discs.blank_discs().size() == 2; }));
    CHECK(c.discs.blank_discs()[0].extension() == QStringLiteral(".dsd"));
    QTemporaryDir directory;
    std::optional<DiscOutcome> outcome;
    QObject::connect(&c.discs, &SessionDiscs::create_finished,
                     [&](const QString&, const DiscOutcome& o) { outcome = o; });
    c.discs.create_disc(QStringLiteral("Acorn DFS 80T DS 400K"), directory.filePath(QStringLiteral("Mine")));
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK(outcome->ok);
    CHECK(outcome->created_path == directory.filePath(QStringLiteral("Mine.dsd")));
    CHECK(QFileInfo::exists(outcome->created_path));

    outcome.reset();
    c.discs.create_disc(QStringLiteral("Acorn DFS 80T DS 400K"),
                        directory.filePath(QStringLiteral("Wrong.ssd")));
    REQUIRE(wait_until([&] { return outcome.has_value(); }));
    CHECK_FALSE(outcome->ok);
    CHECK(outcome->kind == beebium::CREATE_DISC_ERROR_KIND_WRONG_EXTENSION);
}

// --- the Storage mode -----------------------------------------------------------------

namespace {

struct Shown {
    Connected c;
    QTemporaryDir directory;
    ClientSettings settings{directory.filePath(QStringLiteral("Beebium.conf"))};
    bool local = true;
    StorageSidebar sidebar{c.discs, &settings, [this] { return local; }};
    explicit Shown(bool controller = true) : c(controller) {
        sidebar.show();
        REQUIRE(wait_until([&] { return !sidebar.rows().empty() || !controller; }));
    }
    DriveRow* row(int number) { return sidebar.row(number); }
    QString touch(const QString& name) {
        const QString path = directory.filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        return path;
    }
};

}  // namespace

TEST_CASE("Each drive has its row, offering Browse only for a local server",
          "[storage][sidebar]") {
    Shown s;
    REQUIRE(s.sidebar.rows().size() == 2);
    CHECK(s.row(0)->state_text() == QStringLiteral("Empty - Drop disc image here"));
    CHECK(s.row(0)->browse_button()->isEnabled());
    CHECK_FALSE(s.row(0)->eject_button()->isEnabled());
    s.local = false;
    s.row(0)->refresh();
    CHECK(s.row(0)->state_text() == QStringLiteral("Empty - server is on another host"));
    CHECK_FALSE(s.row(0)->browse_button()->isEnabled());
    CHECK(s.row(0)->drop_refusal() == QStringLiteral("Server is on another host"));
}

TEST_CASE("A machine with no disc controller says so", "[storage][sidebar]") {
    Shown s(false);
    REQUIRE(wait_until([&] { return s.sidebar.status_text() == QStringLiteral("No Storage Devices"); }));
    CHECK(s.sidebar.rows().empty());
}

TEST_CASE("A loaded drive shows the disc's name and offers Eject; a taken insert is recent",
          "[storage][sidebar]") {
    Shown s;
    const QString path = s.touch(QStringLiteral("Elite.ssd"));
    s.c.discs.insert(0, path);
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == QStringLiteral("Elite.ssd"); }));
    CHECK(s.row(0)->eject_button()->isEnabled());
    CHECK_FALSE(s.row(0)->browse_button()->isEnabled());
    CHECK(s.row(0)->drop_refusal() == QStringLiteral("Eject disc first"));
    REQUIRE(wait_until([&] { return !s.settings.recent_discs().isEmpty(); }));
    CHECK(s.settings.recent_discs().front() == path);
}

TEST_CASE("A failed insert stays on its row by its kind, with the server's words",
          "[storage][sidebar][errors]") {
    Shown s;
    {
        std::lock_guard lock(s.c.server.disc.calls_mutex);
        s.c.server.disc.fail_next_insert = std::make_pair(
            beebium::DISC_ERROR_KIND_UNRECOGNISED, std::string("no handler recognised notes.txt"));
    }
    s.c.discs.insert(1, s.touch(QStringLiteral("notes.txt")));
    REQUIRE(wait_until([&] { return s.row(1)->message().kind == DriveMessage::Kind::failure; }));
    CHECK(s.row(1)->message().marker == QStringLiteral("Unrecognised format"));
    CHECK(s.row(1)->message().detail == QStringLiteral("no handler recognised notes.txt"));
    CHECK(s.row(1)->message().sentence.endsWith(QLatin1Char('.')));
    // Not recent: the server did not take it.
    CHECK(s.settings.recent_discs().isEmpty());
    // A passing notice does not hide it; only clearing does.
    s.row(1)->show_notice(QStringLiteral("Eject disc first"));
    CHECK(s.row(1)->message().kind == DriveMessage::Kind::failure);
    s.row(1)->clear_message();
    CHECK(s.row(1)->message().kind == DriveMessage::Kind::none);
}

TEST_CASE("An unknown failure kind reads as an honest generic", "[storage][errors]") {
    CHECK(disc_error_marker(beebium::DISC_ERROR_KIND_UNSPECIFIED).isEmpty());
    CHECK(disc_error_marker(99).isEmpty());
    CHECK(disc_error_marker(beebium::DISC_ERROR_KIND_CANNOT_OPEN) == QStringLiteral("Cannot open"));
    CHECK(create_disc_error_marker(beebium::CREATE_DISC_ERROR_KIND_WRONG_EXTENSION) ==
          QStringLiteral("Wrong extension"));
    CHECK(create_disc_error_sentence(0) == QStringLiteral("The disc image could not be created."));
}

TEST_CASE("A notice times out", "[storage][errors]") {
    Shown s;
    s.row(0)->show_notice(QStringLiteral("Drop a single disc image"));
    CHECK(s.row(0)->message().kind == DriveMessage::Kind::notice);
    REQUIRE(wait_until([&] { return s.row(0)->message().kind == DriveMessage::Kind::none; },
                       std::chrono::milliseconds(DriveRow::NOTICE_MS + 2000)));
}

TEST_CASE("A busy eject offers Force Eject after two seconds, and Cancel throughout",
          "[storage][sidebar][eject]") {
    Shown s;
    s.c.discs.insert(0, s.touch(QStringLiteral("a.ssd")));
    REQUIRE(wait_until([&] { return s.row(0)->eject_button()->isEnabled(); }));
    {
        std::lock_guard lock(s.c.server.disc.calls_mutex);
        s.c.server.disc.drive_busy = true;
    }
    s.row(0)->eject_button()->click();
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == QStringLiteral("Ejecting..."); }));
    CHECK(s.row(0)->cancel_button()->isVisibleTo(s.row(0)));
    CHECK_FALSE(s.row(0)->force_button()->isVisibleTo(s.row(0)));
    REQUIRE(wait_until([&] { return s.row(0)->eject_stalled(); }, std::chrono::milliseconds(4000)));
    CHECK(s.row(0)->state_text() == QStringLiteral("Ejecting - drive busy"));
    CHECK(s.row(0)->force_button()->isVisibleTo(s.row(0)));
    s.row(0)->force_button()->click();
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == QStringLiteral("Empty - Drop disc image here"); }));
}

TEST_CASE("A drop is refused for a remote server, then an occupied drive, then many files",
          "[storage][sidebar][drop]") {
    Shown s;
    const QString one = s.touch(QStringLiteral("one.ssd"));
    const QString two = s.touch(QStringLiteral("two.ssd"));
    s.row(0)->drop_files({one, two});
    CHECK(s.row(0)->message().marker == QStringLiteral("Drop a single disc image"));
    s.row(0)->clear_message();
    s.row(0)->drop_files({one});
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == QStringLiteral("one.ssd"); }));
    s.row(0)->drop_files({two});
    CHECK(s.row(0)->message().marker == QStringLiteral("Eject disc first"));
    s.local = false;
    s.row(1)->drop_files({two});
    CHECK(s.row(1)->message().marker == QStringLiteral("Server is on another host"));
}

TEST_CASE("Recent discs are labelled apart, and a missing one cannot be chosen",
          "[storage][sidebar][recent]") {
    CHECK(recent_disc_labels({QStringLiteral("/a/Games/Elite.ssd"), QStringLiteral("/b/Work/Elite.ssd"),
                              QStringLiteral("/a/Repton.ssd")}) ==
          QStringList{QStringLiteral("Elite.ssd (Games)"), QStringLiteral("Elite.ssd (Work)"),
                      QStringLiteral("Repton.ssd")});
    Shown s;
    const QString present = s.touch(QStringLiteral("Here.ssd"));
    s.settings.remember_disc(s.directory.filePath(QStringLiteral("Gone.ssd")));
    s.settings.remember_disc(present);
    Q_EMIT s.row(0)->browse_menu()->aboutToShow();
    QMenu* recent = nullptr;
    for (QAction* action : s.row(0)->browse_menu()->actions()) {
        if (action->menu() && action->text() == QStringLiteral("Recent")) {
            recent = action->menu();
        }
    }
    REQUIRE(recent);
    QStringList enabled;
    QStringList disabled;
    for (QAction* action : recent->actions()) {
        if (!action->isSeparator()) {
            (action->isEnabled() ? enabled : disabled) << action->text();
        }
    }
    CHECK(enabled.contains(QStringLiteral("Here.ssd")));
    CHECK(enabled.contains(QStringLiteral("Clear Recent Discs")));
    CHECK(disabled == QStringList{QStringLiteral("Gone.ssd")});
}

TEST_CASE("A blank disc is made, then inserted, then recent", "[storage][sidebar][blank]") {
    Shown s;
    REQUIRE(wait_until([&] { return s.c.discs.blank_discs().size() == 2; }));
    // As New Blank Disc asks once its file dialog has a name.
    Q_EMIT s.row(1)->blank_disc_requested(1, QStringLiteral("Acorn DFS 80T DS 400K"),
                                          s.directory.filePath(QStringLiteral("Blank")));
    REQUIRE(wait_until([&] { return s.row(1)->state_text() == QStringLiteral("Blank.dsd"); }));
    REQUIRE(wait_until([&] { return !s.settings.recent_discs().isEmpty(); }));
    CHECK(s.settings.recent_discs().front() == s.directory.filePath(QStringLiteral("Blank.dsd")));
}

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

#include "app/SessionDiscs.hpp"

#include "FakeBeebiumServer.hpp"

#include <QDir>
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

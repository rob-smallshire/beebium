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
#include "app/SessionIndicators.hpp"
#include "app/SessionPeripherals.hpp"
#include "app/StorageSidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
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

TEST_CASE("A drive's Browse and Eject are bare icons, Browse with its menu arrow",
          "[storage][sidebar]") {
    Shown s;
    QToolButton* browse = s.row(0)->browse_button();
    QToolButton* eject = s.row(0)->eject_button();
    for (QToolButton* button : {browse, eject}) {
        CHECK(button->autoRaise());  // no border until the pointer is over it
        CHECK(button->toolButtonStyle() == Qt::ToolButtonIconOnly);
        CHECK_FALSE(button->icon().isNull());
        CHECK_FALSE(button->toolTip().isEmpty());
    }
    CHECK(browse->popupMode() == QToolButton::MenuButtonPopup);
    CHECK(browse->menu() != nullptr);
}

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
    // No format or sides under the name: the server knows them, the row
    // does not need them.
    CHECK(s.row(0)->detail_text().isEmpty());
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

TEST_CASE("A failure is dismissed by the cross at the end of its line, in the narrowest sidebar",
          "[storage][sidebar][errors]") {
    Shown s;
    {
        std::lock_guard lock(s.c.server.disc.calls_mutex);
        s.c.server.disc.fail_next_insert = std::make_pair(
            beebium::DISC_ERROR_KIND_UNRECOGNISED, std::string("no handler recognised notes.txt"));
    }
    s.c.discs.insert(1, s.touch(QStringLiteral("notes.txt")));
    REQUIRE(wait_until([&] { return s.row(1)->message().kind == DriveMessage::Kind::failure; }));
    // The sidebar at its narrowest, a large font as the Raspberry Pi's.
    QFont large = s.sidebar.font();
    large.setPointSizeF(large.pointSizeF() * 1.25);
    s.sidebar.setFont(large);
    s.sidebar.resize(220, 400);
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    QToolButton* dismiss = s.row(1)->dismiss_button();
    REQUIRE(dismiss->isVisibleTo(s.row(1)));
    // An icon, not a letter, and inside what the sidebar shows.
    CHECK_FALSE(dismiss->icon().isNull());
    CHECK(dismiss->text().isEmpty());
    CHECK(dismiss->toolTip() == QStringLiteral("Dismiss this error"));
    const QRect shown = QRect(dismiss->mapTo(&s.sidebar, QPoint(0, 0)), dismiss->size());
    CHECK(shown.right() < s.sidebar.width());
    CHECK(s.row(1)->width() <= s.sidebar.width());
    dismiss->click();
    CHECK(s.row(1)->message().kind == DriveMessage::Kind::none);
}

TEST_CASE("A failure's details offer Clear, which ends the failure", "[storage][sidebar][errors]") {
    Shown s;
    {
        std::lock_guard lock(s.c.server.disc.calls_mutex);
        s.c.server.disc.fail_next_insert = std::make_pair(
            beebium::DISC_ERROR_KIND_UNRECOGNISED, std::string("no handler recognised notes.txt"));
    }
    s.c.discs.insert(1, s.touch(QStringLiteral("notes.txt")));
    REQUIRE(wait_until([&] { return s.row(1)->message().kind == DriveMessage::Kind::failure; }));
    s.row(1)->details_button()->click();
    QPushButton* clear = nullptr;
    QWidget* dialog = nullptr;
    REQUIRE(wait_until([&] {
        for (QWidget* top : QApplication::topLevelWidgets()) {
            if (top->isVisible() && top->windowTitle() == QStringLiteral("Disc Problem")) {
                dialog = top;
                for (QPushButton* button : top->findChildren<QPushButton*>()) {
                    if (button->text() == QStringLiteral("Clear")) {
                        clear = button;
                    }
                }
            }
        }
        return clear != nullptr;
    }));
    clear->click();
    CHECK(s.row(1)->message().kind == DriveMessage::Kind::none);
    REQUIRE(wait_until([&] { return !dialog->isVisible(); }));
}

TEST_CASE("An empty drive's hint is set a level down: smaller, not bold, secondary",
          "[storage][sidebar]") {
    Shown s;
    const QLabel* hint = s.row(0)->state_label();
    REQUIRE(s.row(0)->state_text() == QStringLiteral("Empty - Drop disc image here"));
    CHECK(hint->font().pointSizeF() < s.row(0)->font().pointSizeF());
    CHECK(hint->font().weight() <= QFont::Normal);
    CHECK_FALSE(hint->font().bold());
    CHECK_FALSE(hint->isEnabled());  // the secondary text colour
    // A disc's name is the row's main line, at the row's own size.
    s.c.discs.insert(0, s.touch(QStringLiteral("a.ssd")));
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == QStringLiteral("a.ssd"); }));
    CHECK(hint->font().pointSizeF() == s.row(0)->font().pointSizeF());
    CHECK(hint->isEnabled());
}

TEST_CASE("A long disc name ends in an ellipsis at the sidebar's edge, the whole in its tooltip",
          "[storage][sidebar]") {
    Shown s;
    const QString name =
        QStringLiteral("a-disc-image-with-a-very-long-name-longer-than-any-sidebar.ssd");
    s.c.discs.insert(0, s.touch(name));
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == name; }));
    s.sidebar.resize(240, 400);
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    const QLabel* line = s.row(0)->state_label();
    CHECK(line->text().endsWith(QChar(0x2026)));
    CHECK(line->text().size() < name.size());
    CHECK(line->toolTip().endsWith(name));
    CHECK(s.row(0)->width() <= s.sidebar.width());
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

TEST_CASE("The disc's name is not selectable text; its menu is Copy Path and Open Containing "
          "Folder",
          "[storage][sidebar][reveal]") {
    Shown s;
    const QString path = s.touch(QStringLiteral("Elite.ssd"));
    s.c.discs.insert(0, path);
    REQUIRE(wait_until([&] { return s.row(0)->state_text() == QStringLiteral("Elite.ssd"); }));
    for (QLabel* label : s.row(0)->findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("Elite.ssd")) {
            // So a right-click reaches the row's menu, not QLabel's own.
            CHECK(label->textInteractionFlags() == Qt::NoTextInteraction);
        }
    }
    const auto items = [&] {
        std::unique_ptr<QMenu> menu(s.row(0)->make_path_menu());
        REQUIRE(menu);
        CHECK(menu->toolTipsVisible());
        std::vector<std::pair<QString, QAction*>> found;
        for (QAction* action : menu->actions()) {
            action->setParent(nullptr);  // outlive the menu, for the checks below
            found.emplace_back(action->text().remove(QLatin1Char('&')), action);
        }
        return found;
    };
    {
        const auto found = items();
        REQUIRE(found.size() == 2);
        CHECK(found[0].first == QStringLiteral("Copy Path"));
        CHECK(found[1].first == QStringLiteral("Open Containing Folder"));
        CHECK(found[0].second->isEnabled());
        CHECK(found[1].second->isEnabled());
        found[0].second->trigger();
        CHECK(QApplication::clipboard()->text() == path);
        for (const auto& [text, action] : found) {
            delete action;
        }
    }
    // A server on another host: its path means nothing here.
    s.local = false;
    {
        const auto found = items();
        CHECK(found[0].second->isEnabled());
        CHECK_FALSE(found[1].second->isEnabled());
        CHECK(found[1].second->toolTip() == QStringLiteral("The server is on another host"));
        for (const auto& [text, action] : found) {
            delete action;
        }
    }
    // Local, but the file has gone.
    s.local = true;
    QFile::remove(path);
    {
        const auto found = items();
        CHECK_FALSE(found[1].second->isEnabled());
        CHECK(found[1].second->toolTip() == QStringLiteral("The file is not on this computer"));
        for (const auto& [text, action] : found) {
            delete action;
        }
    }
}

// --- storage a peripheral publishes -----------------------------------------------------

namespace {

beebium::ListExtensionsResponse scsi_listing(const QString& image) {
    beebium::ListExtensionsResponse r;
    auto* point = r.add_attachment_points();
    point->set_id("1mhz-bus");
    point->set_display_name("1 MHz Bus");
    auto* adapter = r.add_extensions();
    adapter->set_id("adapter");
    adapter->set_label("Acorn SCSI Host Adapter");
    adapter->add_attaches_to("1mhz-bus");
    adapter->add_attachments()->set_point("1mhz-bus");
    auto* disc = r.add_extensions();
    disc->set_id("disc0");
    disc->set_label("Hard Disc (SCSI ID 0)");
    disc->add_attaches_to("scsi-bus");
    auto* attachment = disc->add_attachments();
    attachment->set_point("scsi-bus");
    attachment->set_provider_id("adapter");
    auto* device = disc->add_storage_devices();
    device->set_id("hdd-0");
    device->set_name("Hard Disc Drive 0");
    device->set_backing_path(image.toStdString());
    device->set_activity_indicator_name("hdd-0-activity-led");
    auto* ram = disc->add_storage_devices();
    ram->set_id("ram-0");
    ram->set_name("RAM Disc Drive 0");
    return r;
}

struct WithPeripherals {
    FakeServer server;
    QTemporaryDir directory;
    QString image = directory.filePath(QStringLiteral("scsi0.dat"));
    SessionDiscs discs;
    SessionPeripherals peripherals;
    SessionIndicators indicators;
    ClientSettings settings{directory.filePath(QStringLiteral("Beebium.conf"))};
    bool local = true;
    std::unique_ptr<StorageSidebar> sidebar;

    explicit WithPeripherals(bool controller = true) {
        {
            QFile file(image);
            REQUIRE(file.open(QIODevice::WriteOnly));
        }
        server.disc.has_controller = controller;
        {
            std::lock_guard lock(server.peripherals.mutex);
            server.peripherals.listing = scsi_listing(image);
        }
        const auto channel = channel_to(server);
        discs.start(channel);
        peripherals.start(channel);
        indicators.start(channel);
        sidebar = std::make_unique<StorageSidebar>(discs, &settings, [this] { return local; });
        sidebar->set_peripheral_storage(peripherals, indicators);
        sidebar->show();
        REQUIRE(wait_until([&] { return sidebar->peripheral_rows().size() == 2; }));
    }
    ~WithPeripherals() {
        discs.stop();
        peripherals.stop();
        indicators.stop();
    }
};

}  // namespace

TEST_CASE("Storage a peripheral publishes is listed below the floppy drives",
          "[storage][sidebar][peripheral]") {
    WithPeripherals w;
    REQUIRE(wait_until([&] { return w.sidebar->rows().size() == 2; }));
    const auto rows = w.sidebar->peripheral_rows();
    CHECK(rows[0]->title_text() == QStringLiteral("Hard Disc Drive 0"));
    CHECK(rows[0]->file_text() == QStringLiteral("scsi0.dat"));
    CHECK(rows[0]->toolTip() == w.image);
    CHECK(rows[1]->title_text() == QStringLiteral("RAM Disc Drive 0"));
    CHECK(rows[1]->file_text() == QStringLiteral("No image"));
    // Below the floppies, once the layout has placed the rows that came
    // last: until it runs they all sit at the top.
    const auto top = [&](const QWidget* row) { return row->mapTo(w.sidebar.get(), QPoint()).y(); };
    wait_until([&] { return top(rows[0]) > top(w.sidebar->rows().back()); });
    CHECK(top(rows[0]) > top(w.sidebar->rows().back()));
    // Read-only: no Browse or Eject of their own.
    CHECK(rows[0]->findChildren<QToolButton*>().isEmpty());
}

TEST_CASE("A peripheral drive's activity shows while its indicator is lit",
          "[storage][sidebar][peripheral]") {
    WithPeripherals w;
    PeripheralStorageRow* row = w.sidebar->peripheral_rows()[0];
    CHECK_FALSE(row->active());
    w.server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{
        {"hdd-0-activity-led", 255}});
    REQUIRE(wait_until([&] { return row->active(); }));
    w.server.indicators.push(std::vector<std::pair<std::string, std::uint32_t>>{
        {"hdd-0-activity-led", 0}});
    REQUIRE(wait_until([&] { return !row->active(); }));
}

TEST_CASE("A peripheral drive's menu copies its path and opens its folder where it is here",
          "[storage][sidebar][peripheral][reveal]") {
    WithPeripherals w;
    PeripheralStorageRow* row = w.sidebar->peripheral_rows()[0];
    {
        std::unique_ptr<QMenu> menu(row->make_path_menu());
        REQUIRE(menu->actions().size() == 2);
        CHECK(menu->actions()[0]->text() == QStringLiteral("Copy Path"));
        CHECK(menu->actions()[1]->text() == QStringLiteral("Open Containing Folder"));
        CHECK(menu->actions()[1]->isEnabled());
        menu->actions()[0]->trigger();
        CHECK(QApplication::clipboard()->text() == w.image);
    }
    w.local = false;
    {
        std::unique_ptr<QMenu> menu(row->make_path_menu());
        CHECK_FALSE(menu->actions()[1]->isEnabled());
        CHECK(menu->actions()[1]->toolTip() == QStringLiteral("The server is on another host"));
    }
    // No image: nothing to copy or open.
    std::unique_ptr<QMenu> none(w.sidebar->peripheral_rows()[1]->make_path_menu());
    CHECK_FALSE(none->actions()[0]->isEnabled());
    CHECK_FALSE(none->actions()[1]->isEnabled());
}

TEST_CASE("Peripheral storage alone is not a machine without storage",
          "[storage][sidebar][peripheral]") {
    WithPeripherals w(false);
    REQUIRE(wait_until([&] { return w.discs.loaded(); }));
    CHECK(w.sidebar->rows().empty());
    CHECK(w.sidebar->status_text().isEmpty());
    CHECK(w.sidebar->peripheral_rows().size() == 2);
}

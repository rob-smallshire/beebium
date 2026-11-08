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

// Edit > Switch to ... Keyboard Mapping, as the macOS client has it: from
// any mapping back to Logical, and from Logical to the mapping before it.
// The previous mapping is the one active just before, whatever made the
// change; it is kept in memory only, and forgotten when its file is gone.

#include "app/DisplayWidget.hpp"
#include "app/KeyboardInput.hpp"
#include "app/KeyboardSidebar.hpp"
#include "app/MainWindow.hpp"
#include "app/MappingCatalogue.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

QString id_of(const char* name) {
    return QString::fromStdString(built_in_mapping(QString::fromLatin1(name))->id);
}

QMenu* edit_menu(MainWindow& window) {
    for (QAction* action : window.menuBar()->actions()) {
        if (action->menu() && action->text().remove(QLatin1Char('&')) == QStringLiteral("Edit")) {
            return action->menu();
        }
    }
    return nullptr;
}

QAction* switch_action(MainWindow& window) {
    QMenu* edit = edit_menu(window);
    return edit && !edit->actions().isEmpty() ? edit->actions().last() : nullptr;
}

QString active_id(MainWindow& window) {
    return QString::fromStdString(window.display().keyboard().mapping().id);
}

void choose_in_sidebar(MainWindow& window, const QString& name) {
    auto* sidebar = window.findChild<KeyboardSidebar*>();
    REQUIRE(sidebar);
    for (int i = 0; i < sidebar->chooser()->count(); ++i) {
        QListWidgetItem* item = sidebar->chooser()->item(i);
        if (item->text() == name) {
            Q_EMIT sidebar->chooser()->itemClicked(item);
            return;
        }
    }
    FAIL("no mapping " << name.toStdString());
}

QString selected_in_sidebar(MainWindow& window) {
    auto* sidebar = window.findChild<KeyboardSidebar*>();
    const auto selected = sidebar->chooser()->selectedItems();
    return selected.size() == 1 ? selected.first()->text() : QString();
}

QString status_name(MainWindow& window) {
    auto* label = window.findChild<QLabel*>(QStringLiteral("mappingName"));
    return label ? label->text() : QString();
}

}  // namespace

TEST_CASE("The switch goes from Logical to the mapping before it, and from any other to Logical",
          "[keyboard][switch]") {
    MappingHistory history(id_of("Logical"));
    CHECK(history.target().isEmpty());  // nothing before it yet
    history.activate(id_of("Thrust"));
    CHECK(history.previous() == id_of("Logical"));
    CHECK(history.target() == id_of("Logical"));
    history.activate(id_of("Arcadians"));
    CHECK(history.previous() == id_of("Thrust"));
    // Not back to Thrust: from anything but Logical, the switch is to Logical.
    CHECK(history.target() == id_of("Logical"));
    history.activate(id_of("Logical"));
    CHECK(history.target() == id_of("Arcadians"));
    // Choosing the active one again changes nothing.
    history.activate(id_of("Logical"));
    CHECK(history.previous() == id_of("Arcadians"));
}

TEST_CASE("A previous mapping that is gone is forgotten", "[keyboard][switch]") {
    MappingHistory history(id_of("Logical"));
    history.activate(QStringLiteral("mine"));
    history.activate(id_of("Logical"));
    REQUIRE(history.target() == QStringLiteral("mine"));
    history.keep_only(mapping_choices(QDir::tempPath() + QStringLiteral("/no-such-dir")));
    CHECK(history.previous().isEmpty());
    CHECK(history.target().isEmpty());
}

TEST_CASE("Edit ends with the switch under a separator, disabled with nothing before Logical",
          "[keyboard][switch][window]") {
    MainWindow window{QRhiWidget::Api::OpenGL};
    QMenu* edit = edit_menu(window);
    REQUIRE(edit);
    const auto actions = edit->actions();
    REQUIRE(actions.size() >= 2);
    CHECK(actions.at(actions.size() - 2)->isSeparator());
    QAction* toggle = actions.last();
    CHECK(toggle->text() == QStringLiteral("Switch to Previous Keyboard Mapping"));
    CHECK_FALSE(toggle->isEnabled());
    CHECK(toggle->shortcuts().contains(QKeySequence(QStringLiteral("Meta+K"))));
    CHECK(toggle->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Alt+K"))));
}

TEST_CASE("The switch bounces to Logical and back, naming where it goes",
          "[keyboard][switch][window]") {
    MainWindow window{QRhiWidget::Api::OpenGL};
    REQUIRE(active_id(window) == id_of("Logical"));
    QAction* toggle = switch_action(window);
    REQUIRE(toggle);

    // A change in the sidebar is recorded as well as one from the menu.
    choose_in_sidebar(window, QStringLiteral("Thrust"));
    CHECK(toggle->isEnabled());
    CHECK(toggle->text() == QStringLiteral("Switch to Logical Keyboard Mapping"));

    toggle->trigger();
    CHECK(active_id(window) == id_of("Logical"));
    CHECK(selected_in_sidebar(window) == QStringLiteral("Logical"));
    CHECK(status_name(window) == QStringLiteral("Logical"));
    CHECK(toggle->text() == QStringLiteral("Switch to Thrust Keyboard Mapping"));

    toggle->trigger();
    CHECK(active_id(window) == id_of("Thrust"));
    CHECK(selected_in_sidebar(window) == QStringLiteral("Thrust"));
    CHECK(status_name(window) == QStringLiteral("Thrust"));

    // From one game mapping to another, the switch still leads to Logical.
    choose_in_sidebar(window, QStringLiteral("Arcadians"));
    CHECK(toggle->text() == QStringLiteral("Switch to Logical Keyboard Mapping"));
    toggle->trigger();
    CHECK(toggle->text() == QStringLiteral("Switch to Arcadians Keyboard Mapping"));
}

TEST_CASE("Switching twice from the sidebar and twice with the switch names each mapping (#289)",
          "[keyboard][switch][window]") {
    // Each change reads the user's files again, replacing the sidebar's list
    // of choices while the change is still being made; under a sanitiser a
    // choice read from the old list is caught.
    MainWindow window{QRhiWidget::Api::OpenGL};
    QAction* toggle = switch_action(window);
    REQUIRE(toggle);
    for (const char* name : {"Thrust", "Arcadians"}) {
        choose_in_sidebar(window, QString::fromLatin1(name));
        CHECK(active_id(window) == id_of(name));
        CHECK(status_name(window) == QString::fromLatin1(name));
    }
    for (const char* name : {"Logical", "Arcadians"}) {
        toggle->trigger();
        CHECK(active_id(window) == id_of(name));
        CHECK(status_name(window) == QString::fromLatin1(name));
    }
}

TEST_CASE("A deleted previous mapping leaves the switch disabled, not pointing at it",
          "[keyboard][switch][window]") {
    // The test's own configuration area (QtTestMain), not the user's.
    const QDir mappings(user_mappings_dirpath());
    REQUIRE(QDir().mkpath(mappings.path()));
    const QString mine_filepath = mappings.filePath(QStringLiteral("mine.json"));
    {
        QFile source(QDir(QStringLiteral(BEEBIUM_KEYBOARD_CORPUS_DIRPATH))
                         .filePath(QStringLiteral("Thrust.json")));
        REQUIRE(source.open(QIODevice::ReadOnly));
        QByteArray text = source.readAll();
        text.replace("\"Thrust\"", "\"Mine\"");
        text.replace("00000000-0000-0000-0000-000000000002", "6a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d");
        QFile mine(mine_filepath);
        REQUIRE(mine.open(QIODevice::WriteOnly));
        mine.write(text);
    }
    MainWindow window{QRhiWidget::Api::OpenGL};
    QAction* toggle = switch_action(window);
    REQUIRE(toggle);
    choose_in_sidebar(window, QStringLiteral("Mine"));
    toggle->trigger();
    REQUIRE(active_id(window) == id_of("Logical"));
    REQUIRE(toggle->text() == QStringLiteral("Switch to Mine Keyboard Mapping"));

    REQUIRE(QFile::remove(mine_filepath));
    // Pressed before the menu is next opened: it finds the mapping gone and
    // stays on Logical.
    toggle->trigger();
    CHECK(active_id(window) == id_of("Logical"));
    CHECK_FALSE(toggle->isEnabled());
    CHECK(toggle->text() == QStringLiteral("Switch to Previous Keyboard Mapping"));
    // And the menu, opened, says the same.
    Q_EMIT edit_menu(window)->aboutToShow();
    CHECK_FALSE(toggle->isEnabled());
}

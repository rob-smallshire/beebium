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

// The Keyboard mode (client specification 7.3, 7.4): the mapping chooser,
// the user's files with the ones that will not load shown as such, and the
// active mapping's labelled keys (#234).

#include "app/KeyboardInput.hpp"
#include "app/KeyboardSidebar.hpp"
#include "app/MappingCatalogue.hpp"

#include "SampleKeyTable.hpp"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

namespace {

void write(const QString& filepath, const QByteArray& text) {
    QFile file(filepath);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(text);
}

QByteArray corpus(const char* name) {
    QFile file(QDir(QStringLiteral(BEEBIUM_KEYBOARD_CORPUS_DIRPATH)).filePath(QString::fromLatin1(name)));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

QStringList reference_rows(const KeyboardSidebar& sidebar) {
    QStringList rows;
    for (int i = 0; i < sidebar.reference()->topLevelItemCount(); ++i) {
        const auto* item = sidebar.reference()->topLevelItem(i);
        rows << item->text(0) + QStringLiteral(" = ") + item->text(1);
    }
    return rows;
}

}  // namespace

TEST_CASE("The built-ins come first, Logical leading, then the user's files", "[keyboard][mode]") {
    QTemporaryDir directory;
    QByteArray mine = corpus("Thrust.json");
    mine.replace("\"Thrust\"", "\"My Thrust\"");
    write(directory.filePath(QStringLiteral("mine.json")), mine);
    write(directory.filePath(QStringLiteral("broken.json")), "{ not json");
    write(directory.filePath(QStringLiteral("old.json")), R"({"version":1,"keys":[]})");
    const auto choices = mapping_choices(directory.path());
    REQUIRE(choices.size() == 9);
    CHECK(choices[0].name == QStringLiteral("Logical"));
    CHECK(choices[0].built_in);
    CHECK(choices[5].built_in);
    // The user's files by file name: broken, mine, old.
    CHECK_FALSE(choices[6].usable());
    CHECK_FALSE(choices[6].problem.isEmpty());
    CHECK(choices[7].usable());
    CHECK_FALSE(choices[7].built_in);
    CHECK(choices[7].name == QStringLiteral("My Thrust"));
    CHECK_FALSE(choices[8].usable());
}

TEST_CASE("A host key is named as a person would", "[keyboard][mode]") {
    MappingEntry entry;
    entry.code = "F12";
    entry.with = MODIFIER_ALT;
    CHECK(host_key_label(entry) == QStringLiteral("Alt+F12"));
    entry.code = "KeyZ";
    entry.with = 0;
    CHECK(host_key_label(entry) == QStringLiteral("Z"));
    entry.code = "ShiftLeft";
    CHECK(host_key_label(entry) == QStringLiteral("Left Shift"));
    entry.code = "Digit7";
    entry.with = MODIFIER_CONTROL | MODIFIER_SHIFT;
    CHECK(host_key_label(entry) == QStringLiteral("Ctrl+Shift+7"));
    entry.code.reset();
    entry.platform = "macos";
    CHECK(host_key_label(entry).isEmpty());  // another platform's key
}

TEST_CASE("Logical labels its five keys with no host namesake", "[keyboard][mode]") {
    const auto mapping = built_in_mapping(QStringLiteral("Logical"));
    REQUIRE(mapping);
    QStringList rows;
    for (const auto& row : mapping_reference(*mapping)) {
        rows << row.host_key + QStringLiteral(" = ") + row.action;
    }
    CHECK(rows == QStringList{QStringLiteral("F10 = f0"), QStringLiteral("F11 = Copy"),
                              QStringLiteral("Alt+F12 = Break"), QStringLiteral("F13 = Shift Lock"),
                              QStringLiteral("Backspace = Delete")});
}

TEST_CASE("Shift Lock on F13 carries a hint that a user mapping reaches it", "[keyboard][mode]") {
    const auto mapping = built_in_mapping(QStringLiteral("Logical"));
    REQUIRE(mapping);
    QStringList hinted;
    for (const auto& row : mapping_reference(*mapping)) {
        if (!row.hint.isEmpty()) {
            hinted << row.action;
            CHECK(row.hint == QStringLiteral(
                                  "Most PC keyboards have no F13: to reach Shift Lock, copy this "
                                  "mapping and put it on a key you have, as Break is on Alt+F12."));
        }
    }
    CHECK(hinted == QStringList{QStringLiteral("Shift Lock")});
    // Shift Lock on a key the keyboard has needs no hint.
    MappingFile moved = *mapping;
    for (auto& entry : moved.keys) {
        if (entry.bbc_key == "Shift Lock") {
            entry.code = "ScrollLock";
        }
    }
    for (const auto& row : mapping_reference(moved)) {
        CHECK(row.hint.isEmpty());
    }
}

TEST_CASE("The mode shows the hint under the table only for a mapping with Shift Lock on F13",
          "[keyboard][mode]") {
    QTemporaryDir directory;
    KeyboardSidebar sidebar(directory.path());
    sidebar.set_active(QString::fromStdString(built_in_mapping(QStringLiteral("Logical"))->id));
    REQUIRE(sidebar.reference_hint());
    CHECK(sidebar.reference_hint()->isVisibleTo(&sidebar));
    CHECK(sidebar.reference_hint()->text().startsWith(QStringLiteral("Most PC keyboards have no F13")));
    sidebar.set_active(QString::fromStdString(built_in_mapping(QStringLiteral("Thrust"))->id));
    CHECK_FALSE(sidebar.reference_hint()->isVisibleTo(&sidebar));
}

TEST_CASE("The mode has no buttons: the mappings folder is managed elsewhere", "[keyboard][mode]") {
    QTemporaryDir directory;
    KeyboardSidebar sidebar(directory.path());
    CHECK(sidebar.findChildren<QPushButton*>().isEmpty());
}

TEST_CASE("The mode shows the active mapping's labels and switches on a choice",
          "[keyboard][mode]") {
    QTemporaryDir directory;
    write(directory.filePath(QStringLiteral("broken.json")), "{ not json");
    KeyboardSidebar sidebar(directory.path());
    const auto default_mapping = built_in_mapping(QStringLiteral("Logical"));
    sidebar.set_active(QString::fromStdString(default_mapping->id));
    CHECK(reference_rows(sidebar).size() == 5);

    std::optional<MappingChoice> chosen;
    QObject::connect(&sidebar, &KeyboardSidebar::mapping_chosen,
                     [&](const MappingChoice& choice) { chosen = choice; });
    QListWidgetItem* thrust = nullptr;
    QListWidgetItem* broken = nullptr;
    for (int i = 0; i < sidebar.chooser()->count(); ++i) {
        QListWidgetItem* item = sidebar.chooser()->item(i);
        if (item->text() == QStringLiteral("Thrust")) {
            thrust = item;
        }
        if (item->text() == QStringLiteral("broken")) {
            broken = item;
        }
    }
    REQUIRE(thrust);
    REQUIRE(broken);
    // One that would not load is there, disabled, saying why.
    CHECK_FALSE(broken->flags() & Qt::ItemIsEnabled);
    CHECK(broken->toolTip().startsWith(QStringLiteral("This mapping cannot be used")));

    Q_EMIT sidebar.chooser()->itemClicked(thrust);
    REQUIRE(chosen);
    CHECK(chosen->name == QStringLiteral("Thrust"));
    CHECK(reference_rows(sidebar).contains(QStringLiteral("Z = Rotate Left")));
    // The list is rebuilt on a choice: find the broken one again.
    broken = nullptr;
    for (int i = 0; i < sidebar.chooser()->count(); ++i) {
        if (sidebar.chooser()->item(i)->text() == QStringLiteral("broken")) {
            broken = sidebar.chooser()->item(i);
        }
    }
    REQUIRE(broken);
    Q_EMIT sidebar.chooser()->itemClicked(broken);
    CHECK(chosen->name == QStringLiteral("Thrust"));  // unchanged

    bool synced = true;
    QObject::connect(&sidebar, &KeyboardSidebar::caps_lock_sync_toggled,
                     [&](bool on) { synced = on; });
    sidebar.set_caps_lock_sync(true);
    sidebar.caps_lock_sync()->click();
    CHECK_FALSE(synced);
}

TEST_CASE("Turning Caps Lock synchronisation off for the session stops the reads",
          "[keyboard][locks]") {
    std::vector<WireEvent> sent;
    KeyboardInput input;
    input.set_host_caps_lock([] { return std::optional<bool>(true); });
    input.attach(beebium::linux_client::testing::sample_key_table(),
                 [&](const std::vector<WireEvent>& events) {
                     sent.insert(sent.end(), events.begin(), events.end());
                 });
    REQUIRE(sent.size() == 1);  // connecting synced
    input.set_caps_lock_sync_override(false);
    input.sync_caps_lock();
    CHECK(sent.size() == 1);
    input.set_caps_lock_sync_override(true);  // switching on is a sync point
    CHECK(sent.size() == 2);
    CHECK(sent.back().action == WireAction::caps_lock_sync);
}

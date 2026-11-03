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
#include <QFrame>
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

TEST_CASE("Logical labels its five keys with no host namesake, Shift Lock on Alt+F11 on Linux", "[keyboard][mode]") {
    const auto mapping = built_in_mapping(QStringLiteral("Logical"));
    REQUIRE(mapping);
    QStringList rows;
    for (const auto& row : mapping_reference(*mapping)) {
        rows << row.host_key + QStringLiteral(" = ") + row.action;
    }
    CHECK(rows == QStringList{QStringLiteral("F10 = f0"), QStringLiteral("F11 = Copy"),
                              QStringLiteral("Alt+F12 = Break"), QStringLiteral("Alt+F11 = Shift Lock"),
                              QStringLiteral("Backspace = Delete")});
}

TEST_CASE("The labelled keys leave out entries that do not apply on Linux", "[keyboard][mode]") {
    auto parsed = parse_mapping_file(R"({"version": 2, "id": "x", "name": "n", "keys": [
        {"code": "F13", "bbc": {"key": "Shift Lock", "action": "Shift Lock"}},
        {"code": "F11", "with": ["Alt"], "when": {"platform": ["linux", "windows"]},
         "bbc": {"key": "Shift Lock", "action": "Shift Lock"}},
        {"code": "F10", "with": ["Alt"], "when": {"platform": "macOS"},
         "bbc": {"key": "f0", "action": "f0"}},
        {"code": "F9", "when": {"keyboardLayout": "ISO"}, "bbc": {"key": "f9", "action": "f9"}}
    ]})");
    REQUIRE(std::holds_alternative<MappingFile>(parsed));
    QStringList rows;
    for (const auto& row : mapping_reference(std::get<MappingFile>(parsed))) {
        rows << row.host_key + QStringLiteral(" = ") + row.action;
    }
    CHECK(rows == QStringList{QStringLiteral("F13 = Shift Lock"),
                              QStringLiteral("Alt+F11 = Shift Lock")});
}

TEST_CASE("The labelled keys carry no prose, not even for a key most keyboards lack",
          "[keyboard][mode]") {
    QTemporaryDir directory;
    KeyboardSidebar sidebar(directory.path());
    // Logical puts Shift Lock on F13.
    sidebar.set_active(QString::fromStdString(built_in_mapping(QStringLiteral("Logical"))->id));
    sidebar.show();
    for (QLabel* label : sidebar.findChildren<QLabel*>()) {
        CHECK_FALSE(label->isVisibleTo(&sidebar));
    }
    QTreeWidget* table = sidebar.reference();
    REQUIRE(table->topLevelItemCount() > 0);
    for (int i = 0; i < table->topLevelItemCount(); ++i) {
        CHECK(table->topLevelItem(i)->toolTip(0).isEmpty());
        CHECK(table->topLevelItem(i)->toolTip(1).isEmpty());
    }
}

TEST_CASE("The mappings are a plain list, the active one its selected row", "[keyboard][mode]") {
    QTemporaryDir directory;
    KeyboardSidebar sidebar(directory.path());
    const auto thrust = built_in_mapping(QStringLiteral("Thrust"));
    sidebar.set_active(QString::fromStdString(thrust->id));
    // Shown, as in a window: showing reads the files again.
    sidebar.show();
    QListWidget* list = sidebar.chooser();
    REQUIRE(list->count() > 0);
    QStringList selected;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        CHECK(item->icon().isNull());  // nothing to push the names out of line
        CHECK_FALSE(item->text().isEmpty());  // no divider without user mappings
        CHECK(item->font().bold() == false);  // no headings
        if (item->isSelected()) {
            selected << item->text();
        }
    }
    CHECK(selected == QStringList{QStringLiteral("Thrust")});
    // Marked whether or not the window has the focus: some palettes draw an
    // inactive selection in the base colour, which would hide it.
    const QPalette palette = list->palette();
    CHECK(palette.color(QPalette::Inactive, QPalette::Highlight) ==
          palette.color(QPalette::Active, QPalette::Highlight));
    CHECK(palette.color(QPalette::Inactive, QPalette::HighlightedText) ==
          palette.color(QPalette::Active, QPalette::HighlightedText));
}

TEST_CASE("The mappings list has no frame and is as tall as its rows, like the labelled keys",
          "[keyboard][mode]") {
    QTemporaryDir directory;
    QByteArray mine = corpus("Thrust.json");
    mine.replace("\"Thrust\"", "\"My Thrust\"");
    write(directory.filePath(QStringLiteral("mine.json")), mine);
    KeyboardSidebar sidebar(directory.path());
    sidebar.set_active(QString::fromStdString(built_in_mapping(QStringLiteral("Logical"))->id));
    sidebar.resize(300, 900);
    sidebar.show();
    QListWidget* list = sidebar.chooser();
    // A plain list on the mode's background, as the table at the foot is.
    CHECK(list->frameShape() == QFrame::NoFrame);
    CHECK(list->frameWidth() == 0);
    CHECK_FALSE(list->viewport()->autoFillBackground());
    CHECK(list->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    CHECK(list->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    // Every row shows, the divider's included, with no empty space below.
    int rows_height = 0;
    for (int i = 0; i < list->count(); ++i) {
        const QRect rect = list->visualItemRect(list->item(i));
        CHECK(rect.top() >= 0);
        CHECK(rect.bottom() < list->viewport()->height());
        rows_height += rect.height();
    }
    CHECK(list->height() == rows_height);
}

TEST_CASE("The user's mappings follow the built-ins after a divider with no text",
          "[keyboard][mode]") {
    QTemporaryDir directory;
    QByteArray mine = corpus("Thrust.json");
    mine.replace("\"Thrust\"", "\"My Thrust\"");
    write(directory.filePath(QStringLiteral("mine.json")), mine);
    KeyboardSidebar sidebar(directory.path());
    QListWidget* list = sidebar.chooser();
    QStringList texts;
    for (int i = 0; i < list->count(); ++i) {
        texts << list->item(i)->text();
    }
    const auto divider = texts.indexOf(QString());
    REQUIRE(divider > 0);
    CHECK(texts.count(QString()) == 1);
    CHECK(texts.at(divider + 1) == QStringLiteral("My Thrust"));
    CHECK_FALSE(list->item(divider)->flags() & Qt::ItemIsSelectable);
}

TEST_CASE("The labelled keys are a bare table at the foot of the mode, the free space above it",
          "[keyboard][mode]") {
    QTemporaryDir directory;
    KeyboardSidebar sidebar(directory.path());
    sidebar.set_active(QString::fromStdString(built_in_mapping(QStringLiteral("Logical"))->id));
    sidebar.resize(300, 900);
    sidebar.show();
    QTreeWidget* table = sidebar.reference();
    CHECK(table->isHeaderHidden());
    for (QLabel* label : sidebar.findChildren<QLabel*>()) {
        CHECK(label->text() != QStringLiteral("Labelled keys"));
    }
    // As tall as its rows, no more.
    int rows_height = 0;
    for (int i = 0; i < table->topLevelItemCount(); ++i) {
        rows_height += table->visualItemRect(table->topLevelItem(i)).height();
    }
    CHECK(table->height() <= rows_height + 2 * table->frameWidth() + 2);
    // The mapping list above, the free space, a divider, then the table.
    QListWidget* list = sidebar.chooser();
    CHECK(table->y() - (list->y() + list->height()) > 200);
    auto* divider = sidebar.findChild<QFrame*>(QStringLiteral("referenceDivider"));
    REQUIRE(divider);
    CHECK(divider->y() > list->y() + list->height());
    CHECK(divider->y() < table->y());
    // The Caps Lock toggle below the table.
    CHECK(sidebar.caps_lock_sync()->y() > table->y());
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

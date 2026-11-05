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

// The configuration editor's sections (client specification 5.2), each an
// editor over its part of the draft, as the macOS client's.

#include "app/configuration/MemorySection.hpp"
#include "app/configuration/StorageSection.hpp"

#include "ConfigurationFixture.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolButton>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

ordered_json model_b_with_disc() {
    return ordered_json::parse(R"({
      "model": "model-b", "name": "BBC Model B with disc", "speed": 1.0,
      "storage": {"fdc_socket": {"id": "acorn-1770"},
                  "floppy_drives": [{"drive": 0, "image_uri": null}, {"drive": 1, "image_uri": null}]},
      "sideways_bank": {"slots": [{"slot": 15, "type": "rom", "image_uri": "bbc-basic_2.rom"}]},
      "econet": null, "extensions": []})");
}

}  // namespace

TEST_CASE("Storage offers the socket's controllers and each drive's disc", "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_disc());
    auto* storage = f.section<StorageSection>();
    QStringList controllers;
    for (int i = 0; i < storage->controller()->count(); ++i) {
        controllers << storage->controller()->itemText(i);
    }
    CHECK(controllers == QStringList({QStringLiteral("Empty"), QStringLiteral("Acorn 1770"),
                                      QStringLiteral("Acorn 8271")}));
    CHECK(storage->controller()->currentText() == QStringLiteral("Acorn 1770"));
    REQUIRE(storage->drive_count() == 2);
    CHECK(storage->drive_field(0)->text().isEmpty());
    bool labelled = false;
    for (QLabel* label : storage->findChildren<QLabel*>()) {
        labelled = labelled || label->text() == QStringLiteral("Floppy drive 1");
    }
    CHECK(labelled);
    CHECK(f.section_names().first() == QStringLiteral("Storage"));
}

TEST_CASE("A disc chosen for a drive is written as a file URI, and an emptied drive as null",
          "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_disc());
    auto* storage = f.section<StorageSection>();
    const int before = f.resolutions;
    f.answer = QStringLiteral("/home/me/discs/Elite Disc.ssd");
    storage->drive_browse(0)->click();
    CHECK(f.dialog->draft().get("/storage/floppy_drives/0/image_uri") ==
          ordered_json("file:///home/me/discs/Elite%20Disc.ssd"));
    CHECK(storage->drive_field(0)->text() == QStringLiteral("/home/me/discs/Elite Disc.ssd"));
    // Judged again, and marked changed.
    REQUIRE(wait_until([&] { return f.resolutions > before; }));
    CHECK(f.section_names().first() == QStringLiteral("Storage •"));
    storage->drive_field(0)->clear();
    Q_EMIT storage->drive_field(0)->editingFinished();
    CHECK(f.dialog->draft().json()["storage"]["floppy_drives"][0]["image_uri"].is_null());
    CHECK(f.section_names().first() == QStringLiteral("Storage"));
}

TEST_CASE("No controller in the socket, no drives", "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_disc());
    auto* storage = f.section<StorageSection>();
    Q_EMIT storage->controller()->activated(0);  // Empty
    CHECK(f.dialog->draft().get("/storage/fdc_socket") == ordered_json{{"id", "none"}});
    CHECK(storage->drive_count() == 0);
}

TEST_CASE("A model with its controller built in has no socket to choose", "[configuration][storage]") {
    auto document = model_b_with_disc();
    document["storage"]["fdc_socket"] = nullptr;
    ConfigurationFixture f(document);
    auto* storage = f.section<StorageSection>();
    CHECK_FALSE(storage->controller()->isVisibleTo(storage));
    CHECK(storage->drive_count() == 2);
}

TEST_CASE("A drive's diagnostics show beneath it, verbatim", "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_disc());
    f.diagnostics = ordered_json::parse(R"([{"severity": "warning", "path": "/storage/floppy_drives/1",
        "location": "Floppy drive 1", "message": "disc image 'gone.ssd' not found"}])");
    f.answer = QStringLiteral("/gone.ssd");
    auto* storage = f.section<StorageSection>();
    storage->drive_browse(1)->click();
    REQUIRE(wait_until([&] { return !f.dialog->diagnostics().empty(); }));
    bool shown = false;
    for (QLabel* label : storage->findChildren<QLabel*>()) {
        shown = shown || label->text() == QStringLiteral("disc image 'gone.ssd' not found");
    }
    CHECK(shown);
}

namespace {

ordered_json model_b_with_dfs() {
    auto document = model_b_with_disc();
    document["sideways_bank"]["slots"] = ordered_json::parse(R"([
        {"slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom"},
        {"slot": 15, "type": "rom", "image_uri": "bbc-basic_2.rom"}])");
    return document;
}

RomDescription described(const char* title, const char* version, QStringList kinds) {
    RomDescription rom;
    rom.recognised = true;
    rom.title = QString::fromUtf8(title);
    rom.version = QString::fromUtf8(version);
    rom.kinds = std::move(kinds);
    return rom;
}

QAction* action(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) {
            return a;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Memory lists the sockets highest priority first, each image by its title",
          "[configuration][memory]") {
    ConfigurationFixture f(model_b_with_dfs(), [](ConfigurationFixture& f) {
        f.roms[QStringLiteral("bbc-basic_2.rom")] = described("BASIC", "", {QStringLiteral("language")});
        f.roms[QStringLiteral("acorn-dfs_2_26.rom")] =
            described("DFS", "2.26", {QStringLiteral("service")});
    });
    auto* memory = f.section<MemorySection>();
    REQUIRE(memory->row_count() == 4);
    REQUIRE(wait_until([&] { return memory->row_title(0)->text() == QStringLiteral("BASIC"); }));
    CHECK(memory->row_label(0)->text() == QStringLiteral("IC101"));
    CHECK(memory->row_caption(0)->text() == QStringLiteral("Slots 3, 7, 11, 15 \u00b7 priority 15 \u00b7 language"));
    CHECK(memory->row_title(1)->text() == QStringLiteral("DFS 2.26"));
    CHECK(memory->row_title(3)->text() == QStringLiteral("Empty"));
    CHECK(memory->row_caption(3)->text() == QStringLiteral("Slots 0, 4, 8, 12 \u00b7 priority 12"));
    CHECK(memory->row_kind(0)->currentText() == QStringLiteral("ROM"));
    CHECK(memory->aliasing_note()->isVisibleTo(memory));
    CHECK_FALSE(memory->row_padlock(0)->isVisibleTo(memory));  // a Model B's RAM has none
    CHECK(f.section_names().at(1) == QStringLiteral("Memory"));
    // Each image is asked about once.
    CHECK(f.described.count(QStringLiteral("bbc-basic_2.rom")) == 1);
}

TEST_CASE("An unrecognised image shows its file name", "[configuration][memory]") {
    auto document = model_b_with_dfs();
    document["sideways_bank"]["slots"][0]["image_uri"] = "file:///home/me/roms/Mystery.rom";
    ConfigurationFixture f(document);
    auto* memory = f.section<MemorySection>();
    CHECK(memory->row_title(1)->text() == QStringLiteral("Mystery.rom"));
    CHECK(memory->row_caption(1)->text() == QStringLiteral("Slots 2, 6, 10, 14 \u00b7 priority 14"));
}

TEST_CASE("Choosing ROM for an empty socket asks for its image first", "[configuration][memory]") {
    ConfigurationFixture f(model_b_with_dfs());
    auto* memory = f.section<MemorySection>();
    QComboBox* kind = memory->row_kind(3);
    const int rom = kind->findData(QStringLiteral("rom"));
    // Cancelled: nothing written, the kind as it was.
    f.answer.reset();
    kind->setCurrentIndex(rom);
    Q_EMIT kind->activated(rom);
    CHECK(kind->currentText() == QStringLiteral("Empty"));
    CHECK(f.dialog->draft().get("/sideways_bank/slots")->size() == 2);
    f.answer = QStringLiteral("/home/me/roms/View.rom");
    Q_EMIT kind->activated(rom);
    const auto slots = *f.dialog->draft().get("/sideways_bank/slots");
    REQUIRE(slots.size() == 3);
    CHECK(slots[2] == ordered_json::parse(R"({"slot": 12, "type": "rom", "image_uri": "file:///home/me/roms/View.rom"})"));
    CHECK(memory->row_title(3)->text() == QStringLiteral("View.rom"));
    CHECK(memory->row_label(3)->text() == QStringLiteral("IC52 \u2022"));
    CHECK(f.section_names().at(1) == QStringLiteral("Memory \u2022"));
}

TEST_CASE("A ROM's image stays as the RAM's preload", "[configuration][memory]") {
    ConfigurationFixture f(model_b_with_dfs());
    auto* memory = f.section<MemorySection>();
    QComboBox* kind = memory->row_kind(1);
    Q_EMIT kind->activated(kind->findData(QStringLiteral("ram")));
    CHECK(memory->row_title(1)->text() == QStringLiteral("Sideways RAM (preload: acorn-dfs_2_26.rom)"));
    Q_EMIT kind->activated(kind->findData(QStringLiteral("empty")));
    CHECK(memory->row_title(1)->text() == QStringLiteral("Empty"));
    Q_EMIT kind->activated(kind->findData(QStringLiteral("ram")));
    CHECK(memory->row_title(1)->text() == QStringLiteral("Sideways RAM"));
}

TEST_CASE("Contents move between sockets, and a changed socket can be reverted",
          "[configuration][memory]") {
    ConfigurationFixture f(model_b_with_dfs());
    auto* memory = f.section<MemorySection>();
    {
        std::unique_ptr<QMenu> menu(memory->row_menu(0));
        CHECK_FALSE(action(menu.get(), QStringLiteral("Move Up"))->isEnabled());
        CHECK(action(menu.get(), QStringLiteral("Revert to Default")) == nullptr);
        action(menu.get(), QStringLiteral("Move Down"))->trigger();
    }
    CHECK(memory->row_title(0)->text() == QStringLiteral("acorn-dfs_2_26.rom"));
    CHECK(memory->row_title(1)->text() == QStringLiteral("bbc-basic_2.rom"));
    {
        std::unique_ptr<QMenu> menu(memory->row_menu(0));
        CHECK_FALSE(action(menu.get(), QStringLiteral("Copy Path"))->isEnabled());  // a library name
        action(menu.get(), QStringLiteral("Revert to Default"))->trigger();
    }
    CHECK(memory->row_title(0)->text() == QStringLiteral("bbc-basic_2.rom"));
}

TEST_CASE("RAM that can be write-protected shows a padlock", "[configuration][memory]") {
    ConfigurationFixture f(model_b_with_dfs(), [](ConfigurationFixture& f) {
        auto schema = ordered_json::parse(f.schema_text);
        for (auto& section : schema["sections"]) {
            if (section["type"] == "sideways_bank") {
                section["has_aliasing"] = false;
                section["sockets"] = ordered_json::parse(R"([
                    {"label": "Slot 15", "slots": [15], "capabilities": ["rom", "ram", "empty", "write_protect"]},
                    {"label": "Slot 14", "slots": [14], "capabilities": ["rom", "ram", "empty", "write_protect"]}])");
            }
        }
        f.schema_text = schema.dump();
    });
    auto* memory = f.section<MemorySection>();
    REQUIRE(memory->row_count() == 2);
    CHECK_FALSE(memory->aliasing_note()->isVisibleTo(memory));
    CHECK(memory->row_caption(0)->text() == QStringLiteral("Slot 15 \u00b7 priority 15"));
    Q_EMIT memory->row_kind(0)->activated(memory->row_kind(0)->findData(QStringLiteral("ram")));
    REQUIRE(memory->row_padlock(0)->isVisibleTo(memory));
    memory->row_padlock(0)->click();
    CHECK(f.dialog->draft().get("/sideways_bank/slots/1/write_protected") == ordered_json(true));
}

TEST_CASE("A socket's diagnostics show beneath it, verbatim", "[configuration][memory]") {
    ConfigurationFixture f(model_b_with_dfs());
    f.diagnostics = ordered_json::parse(R"([{"severity": "error", "path": "/sideways_bank/slots/1",
        "location": "Slot 15", "message": "ROM image 'gone.rom' not found"}])");
    auto* memory = f.section<MemorySection>();
    Q_EMIT memory->row_kind(2)->activated(memory->row_kind(2)->findData(QStringLiteral("ram")));
    REQUIRE(wait_until([&] { return !f.dialog->diagnostics().empty(); }));
    bool shown = false;
    for (QLabel* label : memory->findChildren<QLabel*>()) {
        shown = shown || (label->text() == QStringLiteral("ROM image 'gone.rom' not found") &&
                          label->isVisibleTo(memory));
    }
    CHECK(shown);
}

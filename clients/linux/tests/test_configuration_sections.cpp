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

#include "app/configuration/AttachmentChooser.hpp"
#include "app/configuration/AttachmentPointGroup.hpp"
#include "app/configuration/KeyboardSection.hpp"
#include "app/configuration/MemorySection.hpp"
#include "app/configuration/NetworkSection.hpp"
#include "app/configuration/PeripheralsSection.hpp"
#include "app/configuration/ProcessorSection.hpp"
#include "app/configuration/StorageSection.hpp"

#include "ConfigurationFixture.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QRadioButton>
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

namespace {

ordered_json model_b_with_extensions(const char* extensions) {
    auto document = model_b_with_disc();
    document["extensions"] = ordered_json::parse(extensions);
    return document;
}

bool shows(QWidget* widget, const QString& text) {
    for (QLabel* label : widget->findChildren<QLabel*>()) {
        if (label->text() == text && label->isVisibleTo(widget)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("Peripherals has a group for each of the model's points but the Tube",
          "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-rtc"}])"));
    auto* peripherals = f.section<PeripheralsSection>();
    CHECK(peripherals->group("1mhz-bus"));
    CHECK(peripherals->group("printer-port"));
    CHECK_FALSE(peripherals->group("tube"));
    CHECK_FALSE(peripherals->group("scsi"));  // beneath its adapter
    AttachmentPointGroup* user = peripherals->group("user-port");
    CHECK(user->occupancy()->text() == QStringLiteral("1 of 1"));
    CHECK_FALSE(user->add_button());  // full
    CHECK(user->card("acorn-rtc"));
    CHECK(shows(peripherals->group("1mhz-bus"), QStringLiteral("Nothing attached.")));
    CHECK(peripherals->group("1mhz-bus")->occupancy()->text() == QStringLiteral("0"));
    // Its parameters inline.
    CHECK(peripherals->group("user-port")->forms().size() == 1);
}

TEST_CASE("Add offers what the point takes and appends the name alone", "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions("[]"));
    auto* peripherals = f.section<PeripheralsSection>();
    const int before = f.resolutions;
    f.pick = "hybrid-music-5000";
    peripherals->group("1mhz-bus")->add_button()->click();
    REQUIRE(f.asked);
    CHECK(f.asked->title == QStringLiteral("Add to 1 MHz Bus"));
    CHECK(f.asked->commit == QStringLiteral("Add"));
    CHECK(f.asked->entries.size() == 3);
    CHECK(*f.dialog->draft().get("/extensions") == ordered_json::parse(R"([{"name": "hybrid-music-5000"}])"));
    REQUIRE(wait_until([&] { return peripherals->group("1mhz-bus")->card("hybrid-music-5000"); }));
    REQUIRE(wait_until([&] { return f.resolutions > before; }));
    CHECK(f.section_names().contains(QStringLiteral("Peripherals \u2022")));
}

TEST_CASE("A cancelled Add writes nothing", "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions("[]"));
    auto* peripherals = f.section<PeripheralsSection>();
    f.pick.reset();
    peripherals->group("serial-port")->add_button()->click();
    CHECK(f.dialog->draft().get("/extensions")->empty());
}

TEST_CASE("A single-occupancy point's instance changes, and any can be removed",
          "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "file-printer"}])"));
    auto* peripherals = f.section<PeripheralsSection>();
    f.pick = "tcp-printer";
    peripherals->group("printer-port")->change_button("file-printer")->click();
    REQUIRE(f.asked);
    CHECK(f.asked->title == QStringLiteral("Change Printer Port"));
    CHECK(f.asked->commit == QStringLiteral("Change"));
    CHECK(*f.dialog->draft().get("/extensions") == ordered_json::parse(R"([{"name": "tcp-printer"}])"));
    REQUIRE(wait_until([&] { return peripherals->group("printer-port")->card("tcp-printer"); }));
    peripherals->group("printer-port")->remove_button("tcp-printer")->click();
    CHECK(f.dialog->draft().get("/extensions")->empty());
    REQUIRE(wait_until([&] { return peripherals->group("printer-port")->add_button(); }));
}

TEST_CASE("Discs sit beneath their adapter, with its own occupancy", "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0}}])"));
    auto* peripherals = f.section<PeripheralsSection>();
    AttachmentPointGroup* scsi = peripherals->group("1mhz-bus")->nested("acorn-scsi", "scsi");
    REQUIRE(scsi);
    CHECK(scsi->occupancy()->text() == QStringLiteral("1 of 7"));
    CHECK(scsi->card("scsi-hard-disc"));
    f.pick = "scsi-hard-disc";
    scsi->add_button()->click();
    CHECK((*f.dialog->draft().get("/extensions"))[2] ==
          ordered_json::parse(R"({"name": "scsi-hard-disc", "config": {"id": "scsi-hard-disc-1"}})"));
}

TEST_CASE("A disc naming no adapter is shown unattached, with its diagnostic and Remove",
          "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "adapter-id": "gone"}}])"));
    f.diagnostics = ordered_json::parse(R"([{"severity": "error", "path": "/extensions/1",
        "location": "SCSI hard disc", "message": "no instance 'gone' provides scsi"}])");
    auto* peripherals = f.section<PeripheralsSection>();
    // Never under the adapter that is there.
    CHECK_FALSE(peripherals->group("1mhz-bus")->nested("acorn-scsi", "scsi")->card("scsi-hard-disc"));
    REQUIRE(peripherals->unattached());
    REQUIRE(peripherals->unattached()->card("scsi-hard-disc"));
    // Judged once anything is edited.
    f.dialog->draft_edited();
    REQUIRE(wait_until([&] { return !f.dialog->diagnostics().empty(); }));
    REQUIRE(wait_until([&] {
        return peripherals->unattached() &&
               shows(peripherals->unattached(), QStringLiteral("no instance 'gone' provides scsi"));
    }));
    peripherals->unattached()->remove_button("scsi-hard-disc")->click();
    CHECK(f.dialog->draft().get("/extensions")->size() == 1);
    REQUIRE(wait_until([&] { return !peripherals->unattached(); }));
}

TEST_CASE("An instance's own diagnostics show in its card, an orphan's at the foot",
          "[configuration][peripherals]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-rtc"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0}}])"));
    f.diagnostics = ordered_json::parse(R"([
        {"severity": "error", "path": "/extensions/0", "location": "x", "message": "the user port is taken"},
        {"severity": "error", "path": "/extensions/1", "location": "y", "message": "no SCSI adapter provides scsi"}])");
    auto* peripherals = f.section<PeripheralsSection>();
    f.pick = "acorn-scsi";
    peripherals->group("1mhz-bus")->add_button()->click();
    REQUIRE(wait_until([&] { return !f.dialog->diagnostics().empty(); }));
    REQUIRE(wait_until([&] { return shows(peripherals, QStringLiteral("the user port is taken")); }));
    CHECK(shows(peripherals->group("user-port"), QStringLiteral("the user port is taken")));
}

TEST_CASE("The chooser shows the selected entry's needs and provides", "[configuration][peripherals]") {
    AttachmentChoice choice;
    choice.title = QStringLiteral("Add to 1 MHz Bus");
    choice.commit = QStringLiteral("Add");
    choice.entries.push_back(ChooserEntry{"acorn-scsi", "Acorn SCSI", "An adapter.", {}, {"SCSI Bus"}});
    choice.entries.push_back(ChooserEntry{"x", "X", "", {"Unit", "Port"}, {}});
    AttachmentChooser chooser(choice);
    CHECK_FALSE(chooser.commit()->isEnabled());
    chooser.list()->setCurrentRow(0);
    CHECK(chooser.commit()->isEnabled());
    CHECK(chooser.detail()->text() == QStringLiteral("An adapter.\n\nProvides: SCSI Bus"));
    chooser.list()->setCurrentRow(1);
    CHECK(chooser.detail()->text() == QStringLiteral("Needs: Unit, Port"));
    CHECK(chooser.chosen() == "x");
}

namespace {

ordered_json model_b_with_startup() {
    auto document = model_b_with_disc();
    document["startup"] = ordered_json::parse(R"({"screen_mode": 7, "auto_boot": false})");
    return document;
}

}  // namespace

TEST_CASE("Keyboard shows the startup byte four ways", "[configuration][keyboard]") {
    ConfigurationFixture f(model_b_with_startup());
    auto* keyboard = f.section<KeyboardSection>();
    CHECK(keyboard->screen_mode()->count() == 8);
    CHECK(keyboard->screen_mode()->currentText() == QStringLiteral("7"));
    CHECK_FALSE(keyboard->auto_boot()->isChecked());
    REQUIRE(keyboard->link(7));
    CHECK(keyboard->link(7)->text() == QStringLiteral("Link 1: Filing system"));
    CHECK(keyboard->hex()->text() == QStringLiteral("FF"));
    CHECK(keyboard->decimal()->text() == QStringLiteral("255"));
    CHECK(f.section_names().contains(QStringLiteral("Keyboard")));
}

TEST_CASE("A screen mode or auto-boot writes the friendly pair", "[configuration][keyboard]") {
    ConfigurationFixture f(model_b_with_startup());
    auto* keyboard = f.section<KeyboardSection>();
    Q_EMIT keyboard->screen_mode()->activated(keyboard->screen_mode()->findData(3));
    keyboard->auto_boot()->click();
    CHECK(*f.dialog->draft().get("/startup") == ordered_json::parse(R"({"screen_mode": 3, "auto_boot": true})"));
    CHECK(keyboard->hex()->text() != QStringLiteral("FF"));
    CHECK(f.section_names().contains(QStringLiteral("Keyboard \u2022")));
}

TEST_CASE("A link away from its default writes the whole byte", "[configuration][keyboard]") {
    ConfigurationFixture f(model_b_with_startup());
    auto* keyboard = f.section<KeyboardSection>();
    keyboard->link(7)->click();
    const auto startup = *f.dialog->draft().get("/startup");
    REQUIRE(startup.contains("links"));
    CHECK_FALSE(startup.contains("screen_mode"));
    CHECK(keyboard->decimal()->text() == QString::number(startup["links"].get<int>()));
    // And back by the byte.
    keyboard->hex()->setText(QStringLiteral("&FF"));
    Q_EMIT keyboard->hex()->editingFinished();
    CHECK(*f.dialog->draft().get("/startup") == ordered_json::parse(R"({"screen_mode": 7, "auto_boot": false})"));
    CHECK(keyboard->hex()->text() == QStringLiteral("FF"));
}

TEST_CASE("A refused byte shows the byte as it is", "[configuration][keyboard]") {
    ConfigurationFixture f(model_b_with_startup());
    auto* keyboard = f.section<KeyboardSection>();
    keyboard->decimal()->setText(QStringLiteral("256"));
    Q_EMIT keyboard->decimal()->editingFinished();
    CHECK(keyboard->decimal()->text() == QStringLiteral("255"));
    CHECK_FALSE(f.dialog->draft().changed("/startup"));
}

TEST_CASE("Processor holds the Tube's coprocessor and the speed", "[configuration][processor]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-65c02-coprocessor"}])"));
    auto* processor = f.section<ProcessorSection>();
    REQUIRE(processor->tube());
    CHECK(processor->tube()->card("acorn-65c02-coprocessor"));
    QStringList speeds;
    for (int i = 0; i < processor->speed()->count(); ++i) {
        speeds << processor->speed()->itemText(i);
    }
    CHECK(speeds == QStringList({QStringLiteral("1/4"), QStringLiteral("1/2"), QStringLiteral("1\u00d7"),
                                 QStringLiteral("2\u00d7"), QStringLiteral("4\u00d7"),
                                 QStringLiteral("8\u00d7"), QStringLiteral("Unlimited")}));
    CHECK(processor->speed()->currentText() == QStringLiteral("1\u00d7"));
    f.pick = "acorn-65c102-coprocessor";
    processor->tube()->change_button("acorn-65c02-coprocessor")->click();
    REQUIRE(f.asked);
    CHECK(f.asked->commit == QStringLiteral("Change"));
    CHECK(*f.dialog->draft().get("/extensions") == ordered_json::parse(R"([{"name": "acorn-65c102-coprocessor"}])"));
    REQUIRE(wait_until([&] { return processor->tube()->card("acorn-65c102-coprocessor"); }));
}

TEST_CASE("Speed writes a multiplier or unlimited, and keeps a preset's own", "[configuration][processor]") {
    auto document = model_b_with_disc();
    document["speed"] = 3.0;
    ConfigurationFixture f(document);
    auto* processor = f.section<ProcessorSection>();
    CHECK(processor->speed()->currentText() == QStringLiteral("3\u00d7"));
    Q_EMIT processor->speed()->activated(processor->speed()->count() - 1);
    CHECK(f.dialog->draft().get("/speed") == ordered_json("unlimited"));
    Q_EMIT processor->speed()->activated(processor->speed()->findText(QStringLiteral("1/2")));
    CHECK(f.dialog->draft().get("/speed") == ordered_json(0.5));
    CHECK(f.section_names().contains(QStringLiteral("Processor \u2022")));
}

TEST_CASE("Econet off leaves the rest disabled; on seeds a station and transport",
          "[configuration][network]") {
    ConfigurationFixture f(model_b_with_disc());  // "econet": null
    auto* network = f.section<NetworkSection>();
    CHECK_FALSE(network->econet()->isChecked());
    CHECK_FALSE(network->station_number()->isEnabledTo(network));
    CHECK_FALSE(network->change_transport()->isEnabledTo(network));
    network->econet()->click();
    CHECK(*f.dialog->draft().get("/econet") ==
          ordered_json::parse(R"({"station": "auto", "transport": {"name": "aun", "parameters": {}}})"));
    CHECK(network->station_mode(EconetStation::Mode::automatic)->isChecked());
    CHECK(network->transport_name()->text() != QStringLiteral("None"));
    CHECK(network->transport_form());
    CHECK(f.section_names().contains(QStringLiteral("Network \u2022")));
    network->econet()->click();
    CHECK_FALSE(f.dialog->draft().get("/econet").has_value());  // absent, as null
    CHECK_FALSE(network->transport_form());
}

TEST_CASE("The station is a number held in range, auto, or auto in a range", "[configuration][network]") {
    auto document = model_b_with_disc();
    document["econet"] = ordered_json::parse(R"({"station": 80, "transport": null})");
    ConfigurationFixture f(document);
    auto* network = f.section<NetworkSection>();
    CHECK(network->station_mode(EconetStation::Mode::number)->isChecked());
    CHECK(network->station_number()->text() == QStringLiteral("80"));
    CHECK(network->transport_name()->text() == QStringLiteral("None"));
    network->station_number()->setText(QStringLiteral("300"));
    Q_EMIT network->station_number()->editingFinished();
    CHECK(f.dialog->draft().get("/econet/station") == ordered_json(254));
    CHECK(network->station_number()->text() == QStringLiteral("254"));
    network->station_mode(EconetStation::Mode::range)->click();
    CHECK(f.dialog->draft().get("/econet/station") == ordered_json("auto:1-253"));
    network->station_low()->setText(QStringLiteral("10"));
    Q_EMIT network->station_low()->editingFinished();
    CHECK(f.dialog->draft().get("/econet/station") == ordered_json("auto:10-253"));
}

TEST_CASE("Change Transport offers None first, and a transport starts afresh", "[configuration][network]") {
    auto document = model_b_with_disc();
    document["econet"] = ordered_json::parse(
        R"({"station": 80, "transport": {"name": "aun", "parameters": {"port": 32768}}})");
    ConfigurationFixture f(document);
    auto* network = f.section<NetworkSection>();
    f.pick = "piconet";
    network->change_transport()->click();
    REQUIRE(f.asked);
    CHECK(f.asked->title == QStringLiteral("Change Transport"));
    CHECK(f.asked->commit == QStringLiteral("Change"));
    CHECK(f.asked->selected == "aun");
    REQUIRE(f.asked->entries.size() == 3);
    CHECK(f.asked->entries[0].name.empty());
    CHECK(f.asked->entries[0].display_name == "None");
    CHECK(f.dialog->draft().get("/econet/transport") ==
          ordered_json::parse(R"({"name": "piconet", "parameters": {}})"));
    f.pick = "";
    network->change_transport()->click();
    CHECK_FALSE(f.dialog->draft().get("/econet/transport").has_value());  // absent, as null
    CHECK(network->transport_name()->text() == QStringLiteral("None"));
    CHECK_FALSE(network->transport_form());
}

TEST_CASE("Hard discs: the first disc fits an adapter, the last takes it out", "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_extensions("[]"));
    auto* storage = f.section<StorageSection>();
    REQUIRE(storage->hard_discs()->isVisibleTo(storage));
    CHECK(shows(storage->hard_discs(), QStringLiteral("No hard discs.")));
    storage->add_disc()->click();
    CHECK(*f.dialog->draft().get("/extensions") == ordered_json::parse(R"([{"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0}}])"));
    REQUIRE(wait_until([&] { return storage->disc_count() == 1; }));
    f.answer = QStringLiteral("/home/me/discs/scsi0.dat");
    storage->disc_browse(0)->click();
    CHECK(f.dialog->draft().get("/extensions/1/config/image") == ordered_json("/home/me/discs/scsi0.dat"));
    REQUIRE(wait_until([&] { return storage->disc_field(0)->text() == QStringLiteral("/home/me/discs/scsi0.dat"); }));
    storage->disc_clear(0)->click();
    CHECK_FALSE(f.dialog->draft().get("/extensions/1/config/image").has_value());
    REQUIRE(wait_until([&] { return storage->disc_field(0)->text().isEmpty(); }));
    storage->disc_remove(0)->click();
    CHECK(f.dialog->draft().get("/extensions")->empty());
    REQUIRE(wait_until([&] { return storage->disc_count() == 0; }));
}

TEST_CASE("Hard discs: a duplicate ID is marked, and a bare name is a working copy",
          "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "image": "l3fs-v1_26b.dat"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": 1}}])"));
    auto* storage = f.section<StorageSection>();
    REQUIRE(storage->disc_count() == 2);
    CHECK_FALSE(storage->disc_duplicate(1)->isVisibleTo(storage));
    CHECK(shows(storage->hard_discs(), QStringLiteral("A per-machine working copy is used")));
    CHECK_FALSE(storage->disc_adapter(0));  // one adapter
    Q_EMIT storage->disc_id(1)->activated(storage->disc_id(1)->findData(0));
    CHECK(f.dialog->draft().get("/extensions/2/config/scsi-id") == ordered_json(0));
    REQUIRE(wait_until([&] { return storage->disc_count() == 2 && storage->disc_duplicate(1)->isVisibleTo(storage); }));
    CHECK(storage->disc_duplicate(1)->toolTip() == QStringLiteral("Another disc uses SCSI ID 0"));
}

TEST_CASE("Hard discs: with several adapters, Add asks which and each disc names one",
          "[configuration][storage]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-scsi", "config": {"id": "a"}},
        {"name": "acorn-scsi", "config": {"id": "b"}}])"));
    auto* storage = f.section<StorageSection>();
    REQUIRE(storage->add_disc()->menu());
    QStringList items;
    for (QAction* a : storage->add_disc()->menu()->actions()) {
        items << a->text();
    }
    CHECK(items == QStringList({QStringLiteral("On a"), QStringLiteral("On b")}));
    storage->add_disc()->menu()->actions()[0]->trigger();
    CHECK((*f.dialog->draft().get("/extensions"))[2] ==
          ordered_json::parse(R"({"name": "scsi-hard-disc", "config": {"scsi-id": 0, "adapter-id": "a"}})"));
    REQUIRE(wait_until([&] { return storage->disc_count() == 1; }));
    REQUIRE(storage->disc_adapter(0));
    CHECK(storage->disc_adapter(0)->currentText() == QStringLiteral("a"));
    Q_EMIT storage->disc_adapter(0)->activated(0);  // Last adapter
    CHECK_FALSE(f.dialog->draft().get("/extensions/2/config/adapter-id").has_value());
}

TEST_CASE("Hard discs: a full adapter adds no more", "[configuration][storage]") {
    std::string discs = R"([{"name": "acorn-scsi"})";
    for (int i = 0; i < 7; ++i) {
        discs += R"(, {"name": "scsi-hard-disc", "config": {"scsi-id": )" + std::to_string(i) + "}}";
    }
    discs += "]";
    ConfigurationFixture f(model_b_with_extensions(discs.c_str()));
    auto* storage = f.section<StorageSection>();
    CHECK_FALSE(storage->add_disc()->isEnabled());
    CHECK(storage->add_disc()->toolTip() == QStringLiteral("This machine holds at most 7 hard discs"));
}

TEST_CASE("A floppy drive's Clear empties it", "[configuration][storage]") {
    auto document = model_b_with_disc();
    document["storage"]["floppy_drives"][0]["image_uri"] = "file:///home/me/discs/Elite.ssd";
    ConfigurationFixture f(document);
    auto* storage = f.section<StorageSection>();
    storage->drive_clear(0)->click();
    CHECK(f.dialog->draft().json()["storage"]["floppy_drives"][0]["image_uri"].is_null());
    CHECK(storage->drive_field(0)->text().isEmpty());
}

TEST_CASE("A Tube change marks Processor, not Peripherals; a disc marks Storage",
          "[configuration][markers]") {
    ConfigurationFixture f(model_b_with_extensions(R"([{"name": "acorn-65c02-coprocessor"}])"));
    auto* processor = f.section<ProcessorSection>();
    f.pick = "acorn-65c102-coprocessor";
    processor->tube()->change_button("acorn-65c02-coprocessor")->click();
    REQUIRE(wait_until([&] { return f.section_names().contains(QStringLiteral("Processor \u2022")); }));
    CHECK(f.section_names().contains(QStringLiteral("Peripherals")));
    CHECK(f.section_names().contains(QStringLiteral("Storage")));
    f.section<StorageSection>()->add_disc()->click();
    REQUIRE(wait_until([&] { return f.section_names().contains(QStringLiteral("Storage \u2022")); }));
    CHECK(f.section_names().contains(QStringLiteral("Peripherals \u2022")));  // an adapter and disc
}

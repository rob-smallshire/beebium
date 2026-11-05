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

#include "app/configuration/StorageSection.hpp"

#include "ConfigurationFixture.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>

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

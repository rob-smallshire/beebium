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

// A parameter form in a window (client specification 12.2): the model
// through the live views' renderer, each edit landing in the draft, a
// field's edit landing before another control acts, and Browse asking the
// right chooser.

#include "app/ParameterForm.hpp"
#include "servers/CoreCli.hpp"

#include "WaitUntil.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

const PresetSchema& schema() {
    static const PresetSchema loaded = [] {
        std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                           "describe-preset-schema-model-b.json");
        std::stringstream text;
        text << file.rdbuf();
        return *PresetSchema::parse(text.str());
    }();
    return loaded;
}

MachineDraft disc_draft() {
    return MachineDraft(ordered_json::parse(R"({"model": "model-b", "extensions": [
        {"name": "scsi-hard-disc", "config": {"scsi-id": 0, "image": "", "adapter-id": ""}}]})"));
}

template <typename T>
T* inside(ParameterForm& form, const char* id) {
    QWidget* node = form.renderer()->control_widget(QString::fromLatin1(id));
    INFO(id);
    REQUIRE(node);
    T* found = node->findChild<T*>();
    REQUIRE(found);
    return found;
}

}  // namespace

TEST_CASE("A field's edit lands in the draft when it ends", "[form][widget]") {
    MachineDraft draft = disc_draft();
    ParameterForm form(ParameterFormModel(*schema().extension("scsi-hard-disc"),
                                          "/extensions/0/config", schema()),
                       draft);
    int edits = 0;
    QObject::connect(&form, &ParameterForm::edited, [&] { ++edits; });
    QLineEdit* id = inside<QLineEdit>(form, "param:scsi-id");
    CHECK(id->text() == QStringLiteral("0"));
    CHECK(id->placeholderText() == QStringLiteral("0–7"));
    id->setText(QStringLiteral("4"));
    CHECK(edits == 0);  // not per keystroke
    Q_EMIT id->editingFinished();
    CHECK(edits == 1);
    CHECK(draft.get("/extensions/0/config/scsi-id") == ordered_json(4));
}

TEST_CASE("Browse asks the parameter's chooser, after any edit in progress lands",
          "[form][widget][browse]") {
    MachineDraft draft = disc_draft();
    ParameterForm form(ParameterFormModel(*schema().extension("scsi-hard-disc"),
                                          "/extensions/0/config", schema()),
                       draft);
    std::optional<FormBrowse> asked;
    form.set_chooser([&](const FormBrowse& browse) -> std::optional<QString> {
        asked = browse;
        // The id typed a moment ago is already in the draft.
        CHECK(draft.get("/extensions/0/config/scsi-id") == ordered_json(6));
        return QStringLiteral("/discs/scsi0.dat");
    });
    inside<QLineEdit>(form, "param:scsi-id")->setText(QStringLiteral("6"));
    inside<QPushButton>(form, "browse:image")->click();
    REQUIRE(asked);
    CHECK(asked->chooser == FormBrowse::Chooser::open_file);
    CHECK(asked->label == "Disc image");
    CHECK(draft.get("/extensions/0/config/image") == ordered_json("/discs/scsi0.dat"));
    // Redrawn with the chosen path.
    REQUIRE(wait_until([&] {
        return inside<QLineEdit>(form, "param:image")->text() == QStringLiteral("/discs/scsi0.dat");
    }));
    // A cancelled chooser changes nothing.
    form.set_chooser([](const FormBrowse&) { return std::optional<QString>(); });
    inside<QPushButton>(form, "browse:image")->click();
    CHECK(draft.get("/extensions/0/config/image") == ordered_json("/discs/scsi0.dat"));
}

TEST_CASE("Alternatives switch in the window, showing the chosen one's field",
          "[form][widget][group]") {
    MachineDraft draft(ordered_json::parse(R"({"model": "model-b", "extensions": [
        {"name": "acorn-rtc", "config": {"time": "", "offset": "", "layout": "7bit-year-in-r7"}}]})"));
    ParameterForm form(
        ParameterFormModel(*schema().extension("acorn-rtc"), "/extensions/0/config", schema()),
        draft);
    CHECK(form.renderer()->control_widget(QStringLiteral("param:time")) == nullptr);
    Q_EMIT inside<QComboBox>(form, "group:start")->activated(0);
    REQUIRE(wait_until([&] {
        return form.renderer()->control_widget(QStringLiteral("param:time")) != nullptr;
    }));
    QLineEdit* time = inside<QLineEdit>(form, "param:time");
    time->setText(QStringLiteral("1985-03-15T10:30"));
    Q_EMIT time->editingFinished();
    CHECK(draft.get("/extensions/0/config/time") == ordered_json("1985-03-15T10:30"));
}

TEST_CASE("The server's diagnostics show at their parameter", "[form][widget][diagnostics]") {
    MachineDraft draft = disc_draft();
    ParameterForm form(ParameterFormModel(*schema().extension("scsi-hard-disc"),
                                          "/extensions/0/config", schema()),
                       draft);
    PresetDiagnostic missing;
    missing.path = "/extensions/0/config/image";
    missing.message = "parameter 'image' is required";
    form.set_diagnostics({missing});
    QWidget* note = form.renderer()->control_widget(QStringLiteral("diagnostic:image"));
    REQUIRE(note);
    bool shown = false;
    for (QLabel* label : note->findChildren<QLabel*>()) {
        shown = shown || label->text() == QStringLiteral("parameter 'image' is required");
    }
    CHECK(shown);
}

TEST_CASE("The host's serial ports are read from the server's listing", "[form][serial]") {
    const QByteArray jsonl =
        "{\"path\": \"/dev/ttyUSB0\", \"manufacturer\": \"FTDI\"}\n"
        "not json\n"
        "{\"path\": \"/dev/ttyS0\"}\n";
    CHECK(parse_serial_ports(jsonl) ==
          QStringList({QStringLiteral("/dev/ttyUSB0"), QStringLiteral("/dev/ttyS0")}));
}

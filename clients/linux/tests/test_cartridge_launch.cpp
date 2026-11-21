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

// The sockets a cartridge provides against a real Master 128 (client
// specification 5.9): no cartridge, no socket; a cartridge fitted under
// Peripherals and an image loaded into its bank in Memory, written as a
// sideways_bank entry and running in the machine started from the draft;
// the cartridge removed, and the server's sentence for the image left
// shown under the bank's row.

#include "app/SessionSideways.hpp"
#include "app/configuration/AttachmentPointGroup.hpp"
#include "app/configuration/MemorySection.hpp"
#include "app/configuration/PeripheralsSection.hpp"

#include "ConfigurationFixture.hpp"
#include "RealMaster.hpp"

#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QToolButton>

#include <memory>
#include <optional>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

// resolve-preset's document for a preset, as the dialog's draft starts.
std::optional<ordered_json> resolved(const QString& executable_filepath, const QString& preset) {
    QProcess process;
    process.start(executable_filepath, {QStringLiteral("resolve-preset"), preset, QStringLiteral("--json")});
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        return std::nullopt;
    }
    const auto answer = ordered_json::parse(process.readAllStandardOutput().toStdString(), nullptr, false);
    if (answer.is_discarded() || !answer.contains("preset") || !answer["preset"].is_object()) {
        return std::nullopt;
    }
    // In place: a json converts to an optional two ways, which GCC 15 refuses.
    return std::optional<ordered_json>(std::in_place, answer["preset"]);
}

QByteArray run(const QString& executable_filepath, const QStringList& arguments) {
    QProcess process;
    process.start(executable_filepath, arguments);
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
}

void trigger(MemorySection* memory, int row, const QString& text) {
    std::unique_ptr<QMenu> menu(memory->row_menu(row));
    for (QAction* action : menu->actions()) {
        if (action->text() == text) {
            action->trigger();
            return;
        }
    }
    FAIL("No " << text.toStdString() << " in the row's menu");
}

}  // namespace

TEST_CASE("A cartridge fitted to a real Master 128 brings banks that take an image, and takes them away",
          "[configuration][memory][cartridge][real-server]") {
    const QString executable_filepath = RealMaster::executable_filepath();
    if (executable_filepath.isEmpty()) {
        SKIP("No beebium-master-128 beside BEEBIUM_TEST_SERVER_FILEPATH to run.");
    }
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString rom_filepath = dir.filePath(QStringLiteral("cartridge-test.rom"));
    {
        QFile file(rom_filepath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(16384, '\xff'));
    }
    const auto document = resolved(executable_filepath, QStringLiteral("master-128"));
    REQUIRE(document);
    ConfigurationFixture f(*document, [&](ConfigurationFixture& fixture) {
        fixture.schema_text = run(executable_filepath, {QStringLiteral("describe-preset-schema")}).toStdString();
        fixture.resolving_filepath = executable_filepath;
    });
    auto* memory = f.section<MemorySection>();
    auto* peripherals = f.section<PeripheralsSection>();
    const auto row = [&] { return memory->row_at_slot(0); };
    REQUIRE(row() >= 0);
    CHECK(memory->row_title(row())->text() == QStringLiteral("No cartridge"));

    // Fitted under Peripherals: bank 0 takes an image, as any socket does.
    REQUIRE(peripherals->group("cartridge-rear"));
    f.pick = "acorn-rom-cartridge";
    peripherals->group("cartridge-rear")->add_button()->click();
    REQUIRE(wait_until([&] { return memory->row_title(row())->text() == QStringLiteral("Empty"); }));
    f.answer = rom_filepath;
    trigger(memory, row(), QStringLiteral("Browse for ROM Image..."));
    CHECK(memory->row_title(row())->text() == QStringLiteral("cartridge-test.rom"));
    REQUIRE(wait_until([&] { return f.dialog->create_button()->isEnabled(); }, std::chrono::seconds(30)));
    CHECK(f.dialog->diagnostics().empty());

    // The draft as Create writes it starts a machine with the image in bank 0.
    const QString preset_filepath = dir.filePath(QStringLiteral("draft.json"));
    {
        QFile file(preset_filepath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray::fromStdString(f.dialog->draft().serialise()));
    }
    {
        RealMaster master;
        master.start(preset_filepath);
        SessionSideways sideways;
        sideways.start(master.channel());
        sideways.set_watching(true);
        REQUIRE(wait_until([&] { return sideways.loaded(); }, std::chrono::seconds(10)));
        const SidewaysSocket* bank = nullptr;
        for (const auto& socket : sideways.topology().sockets) {
            for (auto slot : socket.slots) {
                bank = slot == 0 ? &socket : bank;
            }
        }
        REQUIRE(bank);
        CHECK(bank->populated);
        CHECK(QString::fromStdString(bank->image_filepath).endsWith(QStringLiteral("/cartridge-test.rom")));
        sideways.stop();
    }

    // The cartridge removed: the server's sentence for the image left, under its row.
    const int before = f.resolutions;
    REQUIRE(peripherals->group("cartridge-rear")->remove_button("acorn-rom-cartridge"));
    peripherals->group("cartridge-rear")->remove_button("acorn-rom-cartridge")->click();
    REQUIRE(wait_until([&] { return f.resolutions > before && !f.dialog->diagnostics().empty(); },
                       std::chrono::seconds(30)));
    REQUIRE(wait_until([&] { return memory->row_title(row())->text() == QStringLiteral("No cartridge"); }));
    const PresetDiagnostic& orphan = f.dialog->diagnostics().front();
    INFO(orphan.path << ": " << orphan.message);
    CHECK(orphan.error());
    CHECK(orphan.message.find("--sideways") == std::string::npos);
    bool under_row = false;
    for (QLabel* label : memory->findChildren<QLabel*>()) {
        if (label->text() == QString::fromStdString(orphan.message) && label->isVisibleTo(memory)) {
            under_row = under_row || label->parentWidget() == memory->row_title(row())->parentWidget();
        }
    }
    CHECK(under_row);
    CHECK_FALSE(f.dialog->create_button()->isEnabled());
}

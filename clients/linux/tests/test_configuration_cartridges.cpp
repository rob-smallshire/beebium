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

// The sideways sockets a cartridge provides, in the configurator (client
// specification 5.9): "No cartridge" with nothing to load while the slot is
// empty, an ordinary socket naming its cartridge once one is fitted, and
// the way between the Memory row and the slot under Peripherals.

#include "app/configuration/AttachmentPointGroup.hpp"
#include "app/configuration/MemorySection.hpp"
#include "app/configuration/PeripheralsSection.hpp"

#include "ConfigurationFixture.hpp"

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QToolButton>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;
using nlohmann::ordered_json;

namespace {

// The Master 128's schema, cartridges providing its banks 0 to 3.
std::string schema_text() {
    std::ifstream file(std::filesystem::path(BEEBIUM_LINUX_FIXTURES_DIRPATH) /
                       "describe-preset-schema-master-128.json");
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

ordered_json master_128(const char* extensions, const char* slots = "[]") {
    return ordered_json::parse(std::string(R"({"model": "master-128", "name": "BBC Master 128",
        "mos_image_uri": "acorn-mos_3_20.rom", "extensions": )") +
                               extensions + R"(, "sideways_bank": {"slots": )" + slots + "}}");
}

QStringList menu_texts(MemorySection* memory, int row) {
    std::unique_ptr<QMenu> menu(memory->row_menu(row));
    QStringList texts;
    for (QAction* action : menu->actions()) {
        if (!action->isSeparator()) {
            texts << action->text();
        }
    }
    return texts;
}

QStringList kinds(QComboBox* box) {
    QStringList texts;
    for (int i = 0; i < box->count(); ++i) {
        texts << box->itemText(i);
    }
    return texts;
}

QWidget* current_section(ConfigurationFixture& f) {
    return f.dialog->findChild<QStackedWidget*>()->currentWidget();
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

TEST_CASE("A cartridge slot's banks read No cartridge, with nothing to load and the way to the slot",
          "[configuration][memory][cartridge]") {
    ConfigurationFixture f(master_128("[]"), [](ConfigurationFixture& fixture) { fixture.schema_text = schema_text(); });
    auto* memory = f.section<MemorySection>();
    for (int slot : {0, 1, 2, 3}) {
        INFO(slot);
        const int row = memory->row_at_slot(slot);
        REQUIRE(row >= 0);
        CHECK(memory->row_title(row)->text() == QStringLiteral("No cartridge"));
        CHECK_FALSE(memory->row_kind(row)->isVisibleTo(memory));
        CHECK_FALSE(memory->row_actions(row)->isVisibleTo(memory));
        const QStringList menu = menu_texts(memory, row);
        CHECK_FALSE(menu.contains(QStringLiteral("Browse for ROM Image...")));
        REQUIRE(memory->row_cartridge(row));
        CHECK(memory->row_cartridge(row)->isVisibleTo(memory));
    }
    CHECK(memory->row_caption(memory->row_at_slot(0))->text().contains(QStringLiteral("Rear Cartridge Slot")));
    CHECK(memory->row_caption(memory->row_at_slot(2))->text().contains(QStringLiteral("Front Cartridge Slot")));
    // The model's own banks are as they were.
    const int own = memory->row_at_slot(4);
    CHECK(memory->row_title(own)->text() != QStringLiteral("No cartridge"));
    CHECK_FALSE(memory->row_cartridge(own)->isVisibleTo(memory));

    // The way to the slot: Peripherals, at the front slot.
    memory->row_cartridge(memory->row_at_slot(2))->click();
    auto* peripherals = f.section<PeripheralsSection>();
    CHECK(current_section(f) == peripherals);
    CHECK(peripherals->revealed() == "cartridge-front");
}

TEST_CASE("A fitted cartridge's banks are ordinary sockets that name it and load an image",
          "[configuration][memory][cartridge]") {
    ConfigurationFixture f(master_128(R"([{"name": "acorn-rom-cartridge"}])"),
                           [](ConfigurationFixture& fixture) { fixture.schema_text = schema_text(); });
    auto* memory = f.section<MemorySection>();
    const int rear = memory->row_at_slot(0);
    REQUIRE(rear >= 0);
    CHECK(memory->row_title(rear)->text() == QStringLiteral("Empty"));
    CHECK(memory->row_caption(rear)->text().contains(QStringLiteral("Acorn ROM Cartridge")));
    CHECK(memory->row_kind(rear)->isVisibleTo(memory));
    CHECK(kinds(memory->row_kind(rear)) == QStringList{QStringLiteral("ROM"), QStringLiteral("Empty")});
    CHECK_FALSE(memory->row_cartridge(rear)->isVisibleTo(memory));
    CHECK(memory->row_actions(rear)->isVisibleTo(memory));
    CHECK(menu_texts(memory, rear).contains(QStringLiteral("Browse for ROM Image...")));
    // The front slot holds none.
    CHECK(memory->row_title(memory->row_at_slot(2))->text() == QStringLiteral("No cartridge"));

    // An image loaded as into any socket: a sideways_bank entry for the bank.
    f.answer = QStringLiteral("/home/me/roms/Hopper.rom");
    Q_EMIT memory->row_kind(rear)->activated(memory->row_kind(rear)->findData(QStringLiteral("rom")));
    CHECK(memory->row_title(rear)->text() == QStringLiteral("Hopper.rom"));
    const auto slots = f.dialog->draft().get("/sideways_bank/slots");
    REQUIRE(slots);
    bool written = false;
    for (const auto& entry : *slots) {
        written = written || entry == ordered_json::parse(
                                          R"({"slot": 0, "type": "rom", "image_uri": "/home/me/roms/Hopper.rom"})");
    }
    CHECK(written);
}

TEST_CASE("Fitting and removing a cartridge changes its banks, and an orphaned image shows the server's sentence",
          "[configuration][memory][cartridge]") {
    ConfigurationFixture f(master_128("[]"), [](ConfigurationFixture& fixture) { fixture.schema_text = schema_text(); });
    auto* memory = f.section<MemorySection>();
    auto* peripherals = f.section<PeripheralsSection>();
    const auto row = [&] { return memory->row_at_slot(0); };
    REQUIRE(memory->row_title(row())->text() == QStringLiteral("No cartridge"));

    f.pick = "acorn-rom-cartridge";
    peripherals->group("cartridge-rear")->add_button()->click();
    REQUIRE(wait_until([&] { return memory->row_title(row())->text() == QStringLiteral("Empty"); }));
    CHECK(memory->row_kind(row())->isVisibleTo(memory));

    f.answer = QStringLiteral("/home/me/roms/Hopper.rom");
    std::unique_ptr<QMenu> menu(memory->row_menu(row()));
    for (QAction* action : menu->actions()) {
        if (action->text() == QStringLiteral("Browse for ROM Image...")) {
            action->trigger();
        }
    }
    CHECK(memory->row_title(row())->text() == QStringLiteral("Hopper.rom"));
    f.settle();

    // The cartridge gone, the image's entry stays for the server to judge.
    const std::string sentence =
        "Bank 0 holds a ROM image, but no cartridge is fitted in the Rear Cartridge Slot to provide it; "
        "fit one under Peripherals or remove the image.";
    const auto index = [&] {
        const auto slots = f.dialog->draft().get("/sideways_bank/slots");
        REQUIRE(slots);
        for (std::size_t i = 0; i < slots->size(); ++i) {
            if ((*slots)[i].value("slot", -1) == 0) {
                return i;
            }
        }
        FAIL("No entry for bank 0");
        return std::size_t{0};
    }();
    f.diagnostics = ordered_json::array(
        {{{"severity", "error"}, {"path", "/sideways_bank/slots/" + std::to_string(index)}, {"message", sentence}}});
    const int before = f.resolutions;
    REQUIRE(wait_until([&] { return peripherals->group("cartridge-rear") &&
                                    peripherals->group("cartridge-rear")->remove_button("acorn-rom-cartridge"); }));
    peripherals->group("cartridge-rear")->remove_button("acorn-rom-cartridge")->click();
    REQUIRE(wait_until([&] { return f.resolutions > before && !f.dialog->diagnostics().empty(); }));
    REQUIRE(wait_until([&] { return memory->row_title(row())->text() == QStringLiteral("No cartridge"); }));
    CHECK(shows(memory, QString::fromStdString(sentence)));
    // Nothing to load, but the orphan can be cleared.
    CHECK(memory->row_actions(row())->isVisibleTo(memory));
    const QStringList texts = menu_texts(memory, row());
    CHECK_FALSE(texts.contains(QStringLiteral("Browse for ROM Image...")));
    CHECK(texts.contains(QStringLiteral("Clear")));
}

TEST_CASE("A fitted cartridge's view lists the banks it provides, each a way to its row in Memory",
          "[configuration][peripherals][cartridge]") {
    ConfigurationFixture f(master_128(R"([{"name": "acorn-rom-cartridge"}])"),
                           [](ConfigurationFixture& fixture) { fixture.schema_text = schema_text(); });
    auto* peripherals = f.section<PeripheralsSection>();
    AttachmentPointGroup* rear = peripherals->group("cartridge-rear");
    REQUIRE(rear);
    REQUIRE(rear->card("acorn-rom-cartridge"));
    // Its images are loaded in Memory: no fields for them here.
    CHECK(rear->forms().empty());
    CHECK(shows(rear, QStringLiteral("Provides sideways banks 0 and 1.")));
    const auto links = rear->bank_links("acorn-rom-cartridge");
    REQUIRE(links.size() == 2);
    CHECK(links[0]->text() == QStringLiteral("Rear cartridge (bank 0) in Memory"));
    CHECK(links[1]->text() == QStringLiteral("Rear cartridge (bank 1) in Memory"));
    // The front slot holds none: nothing listed.
    REQUIRE(peripherals->group("cartridge-front"));
    CHECK_FALSE(peripherals->group("cartridge-front")->card("acorn-rom-cartridge"));

    links[1]->click();
    auto* memory = f.section<MemorySection>();
    CHECK(current_section(f) == memory);
    CHECK(memory->revealed_row() == memory->row_at_slot(1));
    CHECK(memory->revealed_row() >= 0);
}

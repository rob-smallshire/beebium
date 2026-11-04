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

// The Memory mode (client specification 11, 13.1): the sideways sockets as
// the macOS client's Memory mode lists them, and their protection switches,
// which show only what the server reads back.

#include "app/MainWindow.hpp"
#include "app/MemorySidebar.hpp"
#include "app/SessionSideways.hpp"
#include "app/Sidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <grpcpp/grpcpp.h>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

struct Fixture {
    FakeServer server;
    SessionSideways sideways;

    Fixture() {
        sideways.start(grpc::CreateChannel(server.endpoint().toStdString(),
                                           grpc::InsecureChannelCredentials()));
    }

    void script(const std::function<void(beebium::GetSlotStatusResponse&)>& edit) {
        std::lock_guard lock(server.sideways.status_mutex);
        edit(server.sideways.status);
    }

    beebium::SocketStatus* add_socket(beebium::GetSlotStatusResponse& status,
                                      std::vector<std::uint32_t> slots,
                                      beebium::SidewaysSlotType type, const char* label) {
        auto* socket = status.add_sockets();
        socket->set_socket_index(slots.front());
        for (auto slot : slots) {
            socket->add_aliased_slots(slot);
        }
        socket->set_type(type);
        socket->set_socket_label(label);
        return socket;
    }
};

QStringList visible_texts(QWidget& widget) {
    QStringList texts;
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&widget) && !label->text().isEmpty()) {
            texts << label->text();
        }
    }
    return texts;
}

QAction* menu_action(QMenu* menu, const QString& text) {
    for (QAction* action : menu->actions()) {
        if (action->text().remove(QLatin1Char('&')) == text) {
            return action;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("The Memory mode says when it is reading, and when there is nothing",
          "[memory][mode]") {
    Fixture fixture;
    MemorySidebar memory(fixture.sideways);
    CHECK(visible_texts(memory).contains(QStringLiteral("Reading sideways state…")));
    memory.show();
    REQUIRE(wait_until([&] { return fixture.sideways.loaded(); }));
    CHECK(visible_texts(memory) ==
          QStringList{QStringLiteral("This machine has no sideways memory.")});
}

TEST_CASE("One row per socket, highest priority first: number, name, status",
          "[memory][mode]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        auto* basic = fixture.add_socket(status, {3, 7, 11, 15}, beebium::SIDEWAYS_SLOT_TYPE_ROM,
                                         "IC101");
        basic->set_populated(true);
        basic->set_image_name("/roms/bbc-basic_2.rom");
        basic->mutable_rom_header()->set_recognised(true);
        basic->mutable_rom_header()->set_title("BASIC");
        basic->mutable_rom_header()->add_kinds("language");
        auto* dfs = fixture.add_socket(status, {2, 6, 10, 14}, beebium::SIDEWAYS_SLOT_TYPE_ROM,
                                       "IC100");
        dfs->set_populated(true);
        dfs->set_image_name("/roms/dnfs.rom");
        dfs->mutable_rom_header()->set_recognised(true);
        dfs->mutable_rom_header()->set_title("DFS");
        dfs->mutable_rom_header()->set_version("1.20");
        auto* odd = fixture.add_socket(status, {1, 5, 9, 13}, beebium::SIDEWAYS_SLOT_TYPE_ROM,
                                       "IC88");
        odd->set_populated(true);
        odd->set_image_name("/roms/mystery.bin");
        fixture.add_socket(status, {0, 4, 8, 12}, beebium::SIDEWAYS_SLOT_TYPE_EMPTY, "IC52");
    });
    MemorySidebar memory(fixture.sideways);
    memory.resize(Sidebar::WIDTH, 500);
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 4; }));
    CHECK(visible_texts(memory).first() == QStringLiteral("SIDEWAYS"));
    const auto row = [&](int i) { return memory.row(i); };
    CHECK(row(0)->priority_text() == QStringLiteral("15"));
    CHECK(row(0)->name_text() == QStringLiteral("BASIC"));
    CHECK(row(0)->status_text() == QStringLiteral("ROM"));
    CHECK(row(1)->priority_text() == QStringLiteral("14"));
    CHECK(row(1)->name_text() == QStringLiteral("DFS 1.20"));
    CHECK(row(2)->name_text() == QStringLiteral("mystery.bin"));
    CHECK(row(3)->priority_text() == QStringLiteral("12"));
    CHECK(row(3)->name_text().isEmpty());
    CHECK(row(3)->status_text() == QStringLiteral("—"));
    // The socket's label, its slots and priority, its kinds and its file.
    CHECK(row(0)->toolTip() == QStringLiteral("IC101\nSlots 3, 7, 11, 15  (priority 15)\n"
                                              "Language\n/roms/bbc-basic_2.rom"));
    CHECK(row(3)->toolTip() == QStringLiteral("IC52\nSlots 0, 4, 8, 12  (priority 12)"));
    // No socket label or slot list in the row itself: data in the tooltip.
    CHECK_FALSE(visible_texts(memory).contains(QStringLiteral("IC101")));
}

TEST_CASE("Showing the mode watches the sideways memory; hiding it stops",
          "[memory][mode]") {
    Fixture fixture;
    MemorySidebar memory(fixture.sideways);
    CHECK_FALSE(fixture.sideways.watching());
    memory.show();
    CHECK(fixture.sideways.watching());
    memory.hide();
    CHECK_FALSE(fixture.sideways.watching());
}

TEST_CASE("A RAM socket's padlock shows the server's state and changes only on the read-back",
          "[memory][mode][protection]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_RAM, "SRAM 15");
        fixture.add_socket(status, {14}, beebium::SIDEWAYS_SLOT_TYPE_ROM, "ROM 14");
        auto* write = status.add_protection_groups();
        write->set_id("slot-15");
        write->add_slots(15);
        write->set_supports_write_protect(true);
        auto* rom_write = status.add_protection_groups();
        rom_write->set_id("slot-14");
        rom_write->add_slots(14);
        rom_write->set_supports_write_protect(true);
    });
    MemorySidebar memory(fixture.sideways);
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 2; }));
    QToolButton* padlock = memory.row(0)->padlock();
    REQUIRE(padlock);
    CHECK(padlock->isVisible());
    CHECK_FALSE(memory.row(0)->shows_protected());
    CHECK(padlock->toolTip() == QStringLiteral("Write-protect this sideways RAM"));
    // A ROM is write-protected by nature: no padlock.
    CHECK_FALSE(memory.row(1)->padlock()->isVisible());

    padlock->click();
    CHECK_FALSE(memory.row(0)->shows_protected());  // not the click
    REQUIRE(wait_until([&] { return memory.row(0)->shows_protected(); }));
    CHECK(memory.row(0)->padlock()->toolTip() == QStringLiteral("Allow writes to this sideways RAM"));
    std::lock_guard lock(fixture.server.sideways.status_mutex);
    CHECK(fixture.server.sideways.last_group_id == "slot-15");
    CHECK(fixture.server.sideways.last_engaged);
}

TEST_CASE("A whole-board switch flips every socket it covers together",
          "[memory][mode][protection]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_RAM, "RAM 15");
        fixture.add_socket(status, {14}, beebium::SIDEWAYS_SLOT_TYPE_RAM, "RAM 14");
        auto* board = status.add_protection_groups();
        board->set_id("board");
        board->add_slots(14);
        board->add_slots(15);
        board->set_supports_write_protect(true);
    });
    MemorySidebar memory(fixture.sideways);
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 2; }));
    memory.row(1)->padlock()->click();
    REQUIRE(wait_until([&] {
        return memory.row(0)->shows_protected() && memory.row(1)->shows_protected();
    }));
}

TEST_CASE("The row's menu has Write-Protect and Hide where a switch covers it",
          "[memory][mode][protection]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_RAM, "RAM 15");
        auto* hung = fixture.add_socket(status, {14}, beebium::SIDEWAYS_SLOT_TYPE_ROM, "IC 14");
        hung->set_populated(true);
        fixture.add_socket(status, {13}, beebium::SIDEWAYS_SLOT_TYPE_EMPTY, "IC 13");
        auto* write = status.add_protection_groups();
        write->set_id("slot-15");
        write->add_slots(15);
        write->set_supports_write_protect(true);
        auto* hide = status.add_protection_groups();
        hide->set_id("socket-14");
        hide->add_slots(14);
        hide->set_supports_hide(true);
    });
    MemorySidebar memory(fixture.sideways);
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 3; }));

    std::unique_ptr<QMenu> ram_menu(memory.row(0)->make_menu());
    QAction* write = menu_action(ram_menu.get(), QStringLiteral("Write-Protect"));
    REQUIRE(write);
    CHECK(write->isCheckable());
    CHECK_FALSE(write->isChecked());
    CHECK(menu_action(ram_menu.get(), QStringLiteral("Hide")) == nullptr);

    std::unique_ptr<QMenu> rom_menu(memory.row(1)->make_menu());
    QAction* hide = menu_action(rom_menu.get(), QStringLiteral("Hide"));
    REQUIRE(hide);
    CHECK(menu_action(rom_menu.get(), QStringLiteral("Write-Protect")) == nullptr);
    hide->trigger();
    REQUIRE(wait_until([&] { return fixture.sideways.topology().groups[1].hidden; }));
    {
        std::lock_guard lock(fixture.server.sideways.status_mutex);
        CHECK(fixture.server.sideways.last_kind == beebium::SIDEWAYS_PROTECTION_KIND_HIDE);
    }
    std::unique_ptr<QMenu> hidden_menu(memory.row(1)->make_menu());
    CHECK(menu_action(hidden_menu.get(), QStringLiteral("Hide"))->isChecked());
    // Nothing to do for an empty socket with no switch and no file.
    CHECK_FALSE(memory.row(2)->actionable());
}

TEST_CASE("A machine window offers the Memory mode second, on Meta+2", "[memory][window]") {
    MainWindow window(QRhiWidget::Api::OpenGL);
    auto* sidebar = window.findChild<Sidebar*>();
    REQUIRE(sidebar);
    CHECK(sidebar->has_mode(SidebarMode::memory));
    CHECK(window.findChild<MemorySidebar*>() != nullptr);
}

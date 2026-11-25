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
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <grpcpp/grpcpp.h>

#include <memory>

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
    // The server is on this host.
    MemorySidebar memory(fixture.sideways, [] { return true; });
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
    // A server elsewhere: its paths are not this host's, so not shown.
    MemorySidebar remote(fixture.sideways, [] { return false; });
    REQUIRE(remote.row_count() == 4);
    CHECK(remote.row(0)->toolTip() == QStringLiteral("IC101\nSlots 3, 7, 11, 15  (priority 15)\n"
                                                     "Language"));
    CHECK(remote.row(2)->toolTip() == QStringLiteral("IC88\nSlots 1, 5, 9, 13  (priority 13)"));
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

namespace {

beebium::GetOsRomResponse os_rom(const char* image_uri, const char* rom_name, const char* title) {
    beebium::GetOsRomResponse os;
    os.set_image_uri(image_uri);
    os.set_rom_name(rom_name);
    os.set_size_bytes(16384);
    os.set_title(title);
    return os;
}

}  // namespace

TEST_CASE("The OS section's row is shaped like a socket row: title, ROM, actions",
          "[memory][mode][os]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        auto* basic = fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_ROM, "IC 15");
        basic->set_populated(true);
        basic->mutable_rom_header()->set_recognised(true);
        basic->mutable_rom_header()->set_title("BASIC");
        basic->set_image_name("/roms/basic.rom");
    });
    const QString path = QStringLiteral("/usr/share/beebium/roms/acorn-mos_3_20.rom");
    {
        std::lock_guard lock(fixture.server.sideways.status_mutex);
        fixture.server.sideways.os_rom = os_rom(path.toUtf8().constData(), "acorn-mos_3_20", "Acorn MOS 3.20");
    }
    MemorySidebar memory(fixture.sideways, [] { return true; });
    memory.resize(Sidebar::WIDTH, 500);
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 1 && memory.os_row(); }));
    MemoryRow* os = memory.os_row();
    CHECK(os->isVisibleTo(&memory));
    // The title column, the ROM signifier, and no slot number or ROM name.
    CHECK(os->priority_text().isEmpty());
    CHECK(os->name_text() == QStringLiteral("Acorn MOS 3.20"));
    CHECK(os->status_text() == QStringLiteral("ROM"));
    const QStringList texts = visible_texts(memory);
    REQUIRE(texts.size() >= 4);
    CHECK(texts.mid(0, 4) == QStringList{QStringLiteral("OS"), QStringLiteral("Acorn MOS 3.20"),
                                         QStringLiteral("ROM"), QStringLiteral("SIDEWAYS")});
    CHECK_FALSE(texts.contains(QStringLiteral("acorn-mos_3_20")));
    CHECK_FALSE(texts.contains(QStringLiteral("acorn-mos_3_20.rom")));
    CHECK_FALSE(os->padlock()->isVisibleTo(os));
    // The socket rows' actions, under the same gating.
    REQUIRE(os->actionable());
    CHECK(os->actions()->isVisibleTo(os));
    {
        std::unique_ptr<QMenu> menu(os->make_menu());
        QAction* copy = menu_action(menu.get(), QStringLiteral("Copy Path"));
        REQUIRE(copy);
        REQUIRE(menu_action(menu.get(), QStringLiteral("Open Containing Folder")));
        CHECK(copy->isEnabled());
        copy->trigger();
        CHECK(QApplication::clipboard()->text() == path);
    }
    // The path is the row's tooltip where the server shares this filesystem.
    CHECK(os->toolTip() == path);
    MemorySidebar remote(fixture.sideways, [] { return false; });
    REQUIRE(remote.os_row());
    CHECK(remote.os_row()->toolTip().isEmpty());
}

TEST_CASE("No row's action button in the Memory mode has a tooltip", "[memory][mode][os]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        auto* basic = fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_ROM, "IC 15");
        basic->set_populated(true);
        basic->set_image_name("/roms/basic.rom");
    });
    {
        std::lock_guard lock(fixture.server.sideways.status_mutex);
        fixture.server.sideways.os_rom = os_rom("/roms/mos.rom", "mos", "Acorn MOS 3.20");
    }
    MemorySidebar memory(fixture.sideways, [] { return true; });
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 1 && memory.os_row(); }));
    CHECK(memory.os_row()->actions()->toolTip().isEmpty());
    CHECK(memory.row(0)->actions()->toolTip().isEmpty());
    // The row itself keeps its tooltip.
    CHECK_FALSE(memory.row(0)->toolTip().isEmpty());
}

TEST_CASE("An OS ROM given by path shows only its title", "[memory][mode][os]") {
    Fixture fixture;
    {
        std::lock_guard lock(fixture.server.sideways.status_mutex);
        fixture.server.sideways.os_rom = os_rom("/home/me/roms/my-mos.rom", "", "my-mos");
    }
    MemorySidebar memory(fixture.sideways, [] { return true; });
    memory.show();
    // The OS ROM and the slot status are answered separately, so wait for both.
    const QString no_sideways = QStringLiteral("This machine has no sideways memory.");
    REQUIRE(wait_until([&] {
        return memory.os_row() && memory.os_row()->isVisibleTo(&memory) &&
               visible_texts(memory).contains(no_sideways);
    }));
    const QStringList texts = visible_texts(memory);
    CHECK(memory.os_row()->name_text() == QStringLiteral("my-mos"));
    CHECK_FALSE(texts.contains(QStringLiteral("my-mos.rom")));
    // No sideways memory, but still an OS.
    CHECK(texts.contains(no_sideways));
    CHECK(memory.os_row()->toolTip() == QStringLiteral("/home/me/roms/my-mos.rom"));
}

TEST_CASE("Without an OS ROM from the server there is no OS section", "[memory][mode][os]") {
    Fixture fixture;
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_EMPTY, "IC 15");
    });
    MemorySidebar memory(fixture.sideways);
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 1 && fixture.server.sideways.os_rom_requests == 1; }));
    wait_until([] { return false; }, std::chrono::milliseconds(100));
    CHECK(memory.os_row() == nullptr);
    CHECK_FALSE(visible_texts(memory).contains(QStringLiteral("OS")));
}

TEST_CASE("A row's menu copies its image's path and opens its folder where the file is here",
          "[memory][mode][reveal]") {
    Fixture fixture;
    QTemporaryDir directory;
    const QString image = directory.filePath(QStringLiteral("wordwise.rom"));
    {
        QFile file(image);
        REQUIRE(file.open(QIODevice::WriteOnly));
    }
    fixture.script([&](beebium::GetSlotStatusResponse& status) {
        auto* loaded = fixture.add_socket(status, {15}, beebium::SIDEWAYS_SLOT_TYPE_ROM, "IC 15");
        loaded->set_populated(true);
        loaded->set_image_name(image.toStdString());
        fixture.add_socket(status, {14}, beebium::SIDEWAYS_SLOT_TYPE_RAM, "RAM 14");
        auto* write = status.add_protection_groups();
        write->set_id("slot-14");
        write->add_slots(14);
        write->set_supports_write_protect(true);
    });
    bool local = true;
    MemorySidebar memory(fixture.sideways, [&] { return local; });
    memory.show();
    REQUIRE(wait_until([&] { return memory.row_count() == 2; }));

    {
        std::unique_ptr<QMenu> menu(memory.row(0)->make_menu());
        CHECK(menu->toolTipsVisible());
        QAction* copy = menu_action(menu.get(), QStringLiteral("Copy Path"));
        QAction* reveal = menu_action(menu.get(), QStringLiteral("Open Containing Folder"));
        REQUIRE(copy);
        REQUIRE(reveal);
        CHECK(copy->isEnabled());
        CHECK(reveal->isEnabled());
        copy->trigger();
        CHECK(QApplication::clipboard()->text() == image);
    }
    local = false;
    {
        std::unique_ptr<QMenu> menu(memory.row(0)->make_menu());
        QAction* reveal = menu_action(menu.get(), QStringLiteral("Open Containing Folder"));
        CHECK_FALSE(reveal->isEnabled());
        CHECK(reveal->toolTip() == QStringLiteral("The server is on another host"));
        CHECK(menu_action(menu.get(), QStringLiteral("Copy Path"))->isEnabled());
    }
    local = true;
    QFile::remove(image);
    {
        std::unique_ptr<QMenu> menu(memory.row(0)->make_menu());
        QAction* reveal = menu_action(menu.get(), QStringLiteral("Open Containing Folder"));
        CHECK_FALSE(reveal->isEnabled());
        CHECK(reveal->toolTip() == QStringLiteral("The file is not on this computer"));
    }
    // No file: nothing to copy or open, the switch still there, after a divider.
    {
        std::unique_ptr<QMenu> menu(memory.row(1)->make_menu());
        QStringList texts;
        for (QAction* action : menu->actions()) {
            texts << (action->isSeparator() ? QStringLiteral("|")
                                            : action->text().remove(QLatin1Char('&')));
        }
        CHECK(texts == QStringList{QStringLiteral("Write-Protect"), QStringLiteral("|"),
                                   QStringLiteral("Copy Path"),
                                   QStringLiteral("Open Containing Folder")});
        CHECK_FALSE(menu_action(menu.get(), QStringLiteral("Copy Path"))->isEnabled());
        CHECK_FALSE(menu_action(menu.get(), QStringLiteral("Open Containing Folder"))->isEnabled());
    }
}

TEST_CASE("A machine window offers the Memory mode second, on Meta+2", "[memory][window]") {
    MainWindow window(QRhiWidget::Api::OpenGL);
    auto* sidebar = window.findChild<Sidebar*>();
    REQUIRE(sidebar);
    CHECK(sidebar->has_mode(SidebarMode::memory));
    CHECK(window.findChild<MemorySidebar*>() != nullptr);
}

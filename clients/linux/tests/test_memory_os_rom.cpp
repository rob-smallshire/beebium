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

// The Memory mode's OS section against a real Master 128 (client
// specification 11): the server's own account of its OS ROM.

#include "app/MemorySidebar.hpp"
#include "app/SessionSideways.hpp"

#include "RealMaster.hpp"

#include <QLabel>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

TEST_CASE("A Master 128's Memory mode shows its MOS in the OS section", "[memory][os][real-server]") {
    if (RealMaster::executable_filepath().isEmpty()) {
        SKIP("No beebium-master-128 beside BEEBIUM_TEST_SERVER_FILEPATH to run.");
    }
    RealMaster master;
    master.start(QStringLiteral("master-128"));
    SessionSideways sideways;
    sideways.start(master.channel());
    MemorySidebar memory(sideways);
    memory.show();
    REQUIRE(wait_until([&] { return sideways.loaded() && memory.os_row()->isVisibleTo(&memory); },
                       std::chrono::seconds(10)));
    QStringList texts;
    for (QLabel* label : memory.os_row()->findChildren<QLabel*>()) {
        texts << label->text();
    }
    CHECK(texts == QStringList{QStringLiteral("Acorn MOS 3.20"), QStringLiteral("acorn-mos_3_20")});
    CHECK(memory.os_row()->toolTip().endsWith(QStringLiteral("/acorn-mos_3_20.rom")));
    CHECK(sideways.os_rom()->size_bytes == 16384u);
    CHECK(memory.row_count() > 0);
    sideways.stop();
}

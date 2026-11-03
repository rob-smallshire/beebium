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

// Application shortcuts and the session they are ordered for.

#include "app/Shortcuts.hpp"

#include <QAction>

#include <catch2/catch_test_macros.hpp>

using namespace beebium::linux_client;

TEST_CASE("The Raspberry Pi Desktop is known by its session name", "[shortcuts]") {
    CHECK(raspberry_pi_desktop_session(QStringLiteral("rpd-labwc"), {}));
    CHECK(raspberry_pi_desktop_session(QStringLiteral("rpd-x"), {}));
    CHECK(raspberry_pi_desktop_session({}, QStringLiteral("rpd-labwc")));
    CHECK(raspberry_pi_desktop_session(QStringLiteral("plasma"), QStringLiteral("rpd-x")));
    CHECK_FALSE(raspberry_pi_desktop_session({}, {}));
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("plasma"), QStringLiteral("KDE")));
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("gnome"), QStringLiteral("ubuntu")));
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("LXDE-pi-rpd"), {}));
}

TEST_CASE("An action's Meta form comes first on the Raspberry Pi Desktop, last elsewhere",
          "[shortcuts]") {
    const QKeySequence meta(QStringLiteral("Meta+K"));
    const QKeySequence ctrl_alt(QStringLiteral("Ctrl+Alt+K"));
    CHECK(session_shortcuts(meta, ctrl_alt, true) == QList<QKeySequence>{meta, ctrl_alt});
    CHECK(session_shortcuts(meta, ctrl_alt, false) == QList<QKeySequence>{ctrl_alt, meta});
}

TEST_CASE("An action given its shortcuts shows the first and answers to both", "[shortcuts]") {
    QAction action;
    set_session_shortcuts(&action, QKeySequence(QStringLiteral("Meta+V")),
                          QKeySequence(QStringLiteral("Ctrl+Alt+V")), true);
    CHECK(action.shortcut() == QKeySequence(QStringLiteral("Meta+V")));
    CHECK(action.shortcuts().size() == 2);
    CHECK(action.shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Alt+V"))));
}

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
    CHECK(raspberry_pi_desktop_session(QStringLiteral("rpd-labwc"), {}, {}, false));
    CHECK(raspberry_pi_desktop_session(QStringLiteral("rpd-x"), {}, {}, false));
    CHECK(raspberry_pi_desktop_session({}, QStringLiteral("rpd-labwc"), {}, false));
    CHECK(raspberry_pi_desktop_session(QStringLiteral("plasma"), QStringLiteral("rpd-x"), {}, false));
    CHECK_FALSE(raspberry_pi_desktop_session({}, {}, {}, false));
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("plasma"), QStringLiteral("KDE"),
                                             QStringLiteral("KDE"), false));
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("gnome"), QStringLiteral("ubuntu"),
                                             QStringLiteral("ubuntu:GNOME"), false));
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("LXDE-pi-rpd"), {}, {}, false));
}

TEST_CASE("A labwc or wlroots desktop leaves Meta free as the Raspberry Pi Desktop does",
          "[shortcuts]") {
    CHECK(raspberry_pi_desktop_session({}, {}, QStringLiteral("labwc:wlroots"), false));
    CHECK(raspberry_pi_desktop_session({}, {}, QStringLiteral("wlroots"), false));
    CHECK(raspberry_pi_desktop_session({}, {}, QStringLiteral("LabWC"), false));
    CHECK_FALSE(raspberry_pi_desktop_session({}, {}, QStringLiteral("sway"), false));
}

TEST_CASE("Started outside any session, as over ssh, the installed Raspberry Pi Desktop decides",
          "[shortcuts]") {
    CHECK(raspberry_pi_desktop_session({}, {}, {}, true));
    // Inside another desktop the installed session does not count.
    CHECK_FALSE(raspberry_pi_desktop_session(QStringLiteral("gnome"), QStringLiteral("ubuntu"),
                                             QStringLiteral("ubuntu:GNOME"), true));
}

TEST_CASE("A shortcut shown as written replaces Qt's rendering and keeps the binding",
          "[shortcuts]") {
    QAction action(QStringLiteral("Copy Text from &Graphics"));
    action.setShortcut(QKeySequence(QStringLiteral("Alt+Shift+C")));
    show_shortcut_as(&action, QStringLiteral("Shift+Alt+C"));
    CHECK(action.text() == QStringLiteral("Copy Text from &Graphics\tShift+Alt+C"));
    CHECK(action.shortcut() == QKeySequence(QStringLiteral("Alt+Shift+C")));
    // Shown again, it replaces rather than adds.
    show_shortcut_as(&action, QStringLiteral("Shift+Alt+C"));
    CHECK(action.text() == QStringLiteral("Copy Text from &Graphics\tShift+Alt+C"));
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

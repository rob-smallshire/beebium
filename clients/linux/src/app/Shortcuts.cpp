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

#include "app/Shortcuts.hpp"

#include <QAction>

#include <cstdlib>

namespace beebium::linux_client {

namespace {

QString environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? QString::fromLocal8Bit(value) : QString();
}

}  // namespace

bool raspberry_pi_desktop_session(const QString& desktop_session,
                                  const QString& xdg_session_desktop) {
    const QString prefix = QStringLiteral("rpd-");
    return desktop_session.startsWith(prefix) || xdg_session_desktop.startsWith(prefix);
}

bool raspberry_pi_desktop_session() {
    return raspberry_pi_desktop_session(environment("DESKTOP_SESSION"),
                                        environment("XDG_SESSION_DESKTOP"));
}

QList<QKeySequence> session_shortcuts(const QKeySequence& meta_form,
                                      const QKeySequence& other_form, bool meta_first) {
    return meta_first ? QList<QKeySequence>{meta_form, other_form}
                      : QList<QKeySequence>{other_form, meta_form};
}

void set_session_shortcuts(QAction* action, const QKeySequence& meta_form,
                           const QKeySequence& other_form, bool meta_first) {
    action->setShortcuts(session_shortcuts(meta_form, other_form, meta_first));
}

void set_session_shortcuts(QAction* action, const QKeySequence& meta_form,
                           const QKeySequence& other_form) {
    static const bool meta_first = raspberry_pi_desktop_session();
    set_session_shortcuts(action, meta_form, other_form, meta_first);
}

}  // namespace beebium::linux_client

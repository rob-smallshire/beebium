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

#ifndef BEEBIUM_LINUX_APP_SHORTCUTS_HPP
#define BEEBIUM_LINUX_APP_SHORTCUTS_HPP

#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;

namespace beebium::linux_client {

// Application shortcuts. The BBC Micro has Ctrl and Shift but no Alt or Meta,
// so every shortcut of the machine window carries Alt or Meta and none is on
// Ctrl or Shift alone. Meta plays the part Command plays on macOS. Each
// action answers to two forms, a Meta form and a Ctrl+Alt form (the home
// window, with no machine, keeps its standard Ctrl form beside the Meta one).
// A menu shows only an action's first shortcut, so the forms are ordered for
// the session: the Meta form first on the Raspberry Pi Desktop, whose
// keyboards have the key and whose compositor leaves Meta with a letter
// free, the other first elsewhere. A wrong guess only changes which of two
// working forms the menu shows. The copies of the screen's text are the
// exception: they carry Alt, the selection modifier, and have one form each.

// Whether the session is the Raspberry Pi Desktop, or another desktop on
// labwc or wlroots, which leave Meta with a letter free: DESKTOP_SESSION or
// XDG_SESSION_DESKTOP starting "rpd-" (rpd-labwc, rpd-x), or
// XDG_CURRENT_DESKTOP naming labwc or wlroots. A process started outside the
// session, over ssh say, sees none of these, so the installed Raspberry Pi
// Desktop session (`rpd_session_installed`) decides then.
bool raspberry_pi_desktop_session(const QString& desktop_session,
                                  const QString& xdg_session_desktop,
                                  const QString& xdg_current_desktop,
                                  bool rpd_session_installed);
// The same, from this process's environment and the installed sessions.
bool raspberry_pi_desktop_session();

// An action's forms, the one the menu shows first.
QList<QKeySequence> session_shortcuts(const QKeySequence& meta_form,
                                      const QKeySequence& other_form, bool meta_first);

void set_session_shortcuts(QAction* action, const QKeySequence& meta_form,
                           const QKeySequence& other_form, bool meta_first);
// Ordered for this process's session.
void set_session_shortcuts(QAction* action, const QKeySequence& meta_form,
                           const QKeySequence& other_form);

// Show `shown` as the action's shortcut in a menu instead of Qt's rendering
// of its first binding: Qt orders modifiers its own way, and Shift+Alt+C
// reads as Alt+C with Shift. The bindings are unchanged.
void show_shortcut_as(QAction* action, const QString& shown);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SHORTCUTS_HPP

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

#ifndef BEEBIUM_LINUX_APP_DIALOG_LOCATIONS_HPP
#define BEEBIUM_LINUX_APP_DIALOG_LOCATIONS_HPP

#include <QFileDialog>
#include <QString>
#include <QStringList>

class QWidget;

namespace beebium::linux_client {

class ClientSettings;

// An open dialog chooses something that exists; a save dialog names
// something new. The two never share a remembered folder.
enum class DialogDirection { open, save };

// Where each file dialog starts (client specification 3.6): the folder the
// user last chose in that same dialog function and direction, kept as a
// client setting; else, and when that folder is gone or, for a save, not
// writable, the Documents folder. Never the home folder, and never a
// folder of the server's own: its blank disc and tape templates, or the
// per-user tree it manages. A file the user chooses there is honoured, but
// the remembered folder stays as it was.
class DialogLocations {
public:
    struct Rules {
        QString documents_dirpath;
        // Folders, and all within them, never started in or remembered,
        // besides any blank-discs or blank-tapes folder (or a source tree's
        // discs/blank and tapes/blank).
        QStringList excluded_dirpaths;
    };
    // This host's: its Documents folder, and the server's per-user tree
    // (BEEBIUM_USER_DIRPATH, else $XDG_CONFIG_HOME/beebium).
    static Rules host_rules();

    // With no settings, every dialog starts in Documents and nothing is
    // remembered.
    explicit DialogLocations(ClientSettings* settings);
    DialogLocations(ClientSettings* settings, Rules rules);

    QString start_dirpath(const QString& function, DialogDirection direction) const;
    // Where a save dialog starts, with its proposed name.
    QString start_filepath(const QString& function, const QString& proposed_name) const;
    // A file, or an item that is a folder (a machine), was chosen: the
    // folder holding it is remembered.
    void remember_file(const QString& function, DialogDirection direction, const QString& chosen_path);
    // A folder was chosen as a folder (a destination): it is remembered.
    void remember_folder(const QString& function, DialogDirection direction, const QString& chosen_dirpath);
    bool excluded(const QString& dirpath) const;

private:
    void remember(const QString& function, DialogDirection direction, const QString& dirpath);

    ClientSettings* settings_;
    Rules rules_;
};

// The dialogs, each starting where DialogLocations says and remembering
// what was chosen. Empty when cancelled.
QString open_file(QWidget* parent, DialogLocations locations, const QString& function,
                  const QString& title, const QString& filter = QString());
QString save_file(QWidget* parent, DialogLocations locations, const QString& function,
                  const QString& title, const QString& proposed_name,
                  const QString& filter = QString(), QFileDialog::Options options = {});
// A folder to use as a folder (remembered itself), or, with `item`, one
// chosen as a thing in its own right (a machine; its folder remembered).
QString choose_directory(QWidget* parent, DialogLocations locations, const QString& function,
                         DialogDirection direction, const QString& title, bool item = false);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_DIALOG_LOCATIONS_HPP

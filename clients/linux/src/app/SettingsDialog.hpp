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

#ifndef BEEBIUM_LINUX_APP_SETTINGS_DIALOG_HPP
#define BEEBIUM_LINUX_APP_SETTINGS_DIALOG_HPP

#include <QDialog>

class QLabel;
class QLineEdit;

namespace beebium::linux_client {

class ClientSettings;

// Tools > Configure Beebium: where this host's servers and ROMs are
// (client specification 13.3; docs/deployment.md). Saved on OK.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(ClientSettings& settings, QWidget* parent = nullptr);

    void accept() override;

private:
    void show_found();

    ClientSettings& settings_;
    QLineEdit* servers_;
    QLabel* servers_found_;
    QLineEdit* roms_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SETTINGS_DIALOG_HPP

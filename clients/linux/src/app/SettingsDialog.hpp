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

#include "app/ColourChoice.hpp"
#include "app/MachinesPage.hpp"
#include "display/VideoSettings.hpp"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace beebium::linux_client {

class ClientSettings;

// Settings > Configure Beebium (client specification 13.3): a Paths page,
// where this host's servers and ROMs are (docs/deployment.md), and a Video
// page, the defaults new machine windows start with (6.6). Saved on OK.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // With a library, a Machines page whose actions take effect at once:
    // once one has, Cancel says Close, since it undoes none of them.
    explicit SettingsDialog(ClientSettings& settings, PresetLibraryHost library = {},
                            QWidget* parent = nullptr);

    MachinesPage* machines() const { return machines_; }
    QPushButton* cancel_button() const { return cancel_; }

    void accept() override;
    // The Video page's colour picker; the platform's by default.
    void set_colour_chooser(const ColourChoice::Chooser& chooser) { boxing_->set_chooser(chooser); }

private:
    void show_found();

    ClientSettings& settings_;
    QLineEdit* servers_;
    QLabel* servers_found_;
    QLineEdit* roms_;
    QComboBox* default_style_;
    QComboBox* default_pixels_;
    ColourChoice* boxing_;
    MachinesPage* machines_ = nullptr;
    QPushButton* cancel_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SETTINGS_DIALOG_HPP

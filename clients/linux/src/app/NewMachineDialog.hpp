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

#ifndef BEEBIUM_LINUX_APP_NEW_MACHINE_DIALOG_HPP
#define BEEBIUM_LINUX_APP_NEW_MACHINE_DIALOG_HPP

#include "servers/CoreCli.hpp"
#include "servers/ServerLocator.hpp"

#include <QDialog>

#include <optional>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace beebium::linux_client {

// The presets that can be launched here: those whose server executable this
// host has (client specification 5.1: a preset whose executable is absent is
// omitted), built-in first, then the user's, each in the server's order.
std::vector<PresetListing> launchable_presets(
    const std::vector<PresetListing>& presets,
    const std::function<bool(const QString& model)>& has_server);

// Choose a preset to launch (5.1). The catalogue, descriptions and
// thumbnails are the server's: list-presets, and show-preset --thumbnail for
// a preset that has one (never for one that has none).
class NewMachineDialog : public QDialog {
    Q_OBJECT

public:
    NewMachineDialog(ServerLocation servers, QString thumbnail_cache_dirpath,
                     QWidget* parent = nullptr);

    std::optional<PresetListing> chosen() const;

private:
    void load();
    void show_presets(const std::vector<PresetListing>& presets);
    void fetch_thumbnail(QTreeWidgetItem* item, const PresetListing& preset);

    ServerLocation servers_;
    QString thumbnail_cache_dirpath_;
    CoreCli cli_;
    QTreeWidget* list_;
    QLabel* status_;
    QPushButton* launch_;
    std::vector<PresetListing> presets_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_NEW_MACHINE_DIALOG_HPP

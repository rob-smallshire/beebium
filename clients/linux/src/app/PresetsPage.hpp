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

#ifndef BEEBIUM_LINUX_APP_PRESETS_PAGE_HPP
#define BEEBIUM_LINUX_APP_PRESETS_PAGE_HPP

#include "servers/CoreCli.hpp"
#include "servers/ServerLocator.hpp"

#include <QSet>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QListWidgetItem;
class QPushButton;

namespace beebium::linux_client {

class CardGrid;

// The presets that can be launched here: those whose server executable this
// host has (client specification 5.1: a preset whose executable is absent is
// omitted), built-in first, then the user's, each in the server's order.
std::vector<PresetListing> launchable_presets(
    const std::vector<PresetListing>& presets,
    const std::function<bool(const QString& model)>& has_server);

// The home window's presets catalogue (5.1, 13.4): a card per preset, and
// Launch. The catalogue, descriptions and thumbnails are the server's:
// list-presets, and show-preset --thumbnail for a preset that has one (never
// for one that has none).
class PresetsPage : public QWidget {
    Q_OBJECT

public:
    explicit PresetsPage(QString thumbnail_cache_dirpath, QWidget* parent = nullptr);

    // Where the servers are, or that none were found; reads the catalogue
    // afresh.
    void set_servers(std::optional<ServerLocation> servers);
    // While a launch is under way, nothing else is launched.
    void set_launching(bool launching);

    std::optional<PresetListing> chosen() const;
    const std::vector<PresetListing>& presets() const { return presets_; }
    CardGrid* grid() const { return grid_; }
    QString status_text() const;
    // The server's own words, when reading the presets failed.
    QString detail_text() const;
    bool offering_configure() const;

Q_SIGNALS:
    void launch_requested(const beebium::linux_client::PresetListing& preset);
    // No servers were found: the user may say where they are.
    void configure_requested();

private:
    void load();
    void show_presets(const std::vector<PresetListing>& presets);
    void fetch_thumbnail(QListWidgetItem* item, const PresetListing& preset);
    void launch_chosen();

    std::optional<ServerLocation> servers_;
    QString thumbnail_cache_dirpath_;
    CoreCli cli_;
    CardGrid* grid_;
    QLabel* status_;
    QLabel* detail_;
    QPushButton* configure_;
    std::vector<PresetListing> presets_;
    // Presets whose thumbnail could not be made: never asked for again.
    QSet<QString> thumbnail_failed_;
    bool launching_ = false;
    int generation_ = 0;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PRESETS_PAGE_HPP

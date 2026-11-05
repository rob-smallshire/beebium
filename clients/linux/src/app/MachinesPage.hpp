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

#ifndef BEEBIUM_LINUX_APP_MACHINES_PAGE_HPP
#define BEEBIUM_LINUX_APP_MACHINES_PAGE_HPP

#include "servers/CoreCli.hpp"

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

namespace beebium::linux_client {

// What the preset library needs of this host's servers, as functions so
// tests can stand in for them. Each command reports its success, or the
// server's own words when it failed.
struct PresetLibraryHost {
    using Presets = std::function<void(std::vector<PresetListing> presets, QString error)>;
    using Done = std::function<void(bool ok, QString error)>;

    std::function<void(Presets done)> list_presets;
    std::function<void(const PresetListing& source, const QString& name, Done done)> duplicate;
    std::function<void(const PresetListing& preset, const QString& filepath, Done done)> export_preset;
    std::function<void(const PresetListing& preset, Done done)> remove;
};

// Settings > Machines (client specification 13.3), the preset library as
// the macOS client's: the built-in presets and the user's, each by release
// date then name, with Duplicate..., Export..., Delete... for the user's
// own, and Open Containing Folder, as buttons and as a row's menu. Each
// takes effect at once.
class MachinesPage : public QWidget {
    Q_OBJECT

public:
    explicit MachinesPage(PresetLibraryHost host, QWidget* parent = nullptr);

    // Read the library afresh.
    void reload();

    // The questions the actions ask: the platform's dialogs by default;
    // tests answer them. Nothing, or false, when the user cancels.
    using AskName = std::function<std::optional<QString>(const QString& suggested, const QStringList& taken)>;
    using AskFilepath = std::function<std::optional<QString>(const QString& suggested_filepath)>;
    using Confirm = std::function<bool(const QString& name)>;
    using Reveal = std::function<void(const QString& filepath)>;
    void set_ask_name(AskName ask) { ask_name_ = std::move(ask); }
    void set_ask_filepath(AskFilepath ask) { ask_filepath_ = std::move(ask); }
    void set_confirm_delete(Confirm confirm) { confirm_delete_ = std::move(confirm); }
    void set_reveal(Reveal reveal) { reveal_ = std::move(reveal); }

    QListWidget* built_in_list() const { return built_in_; }
    QListWidget* user_list() const { return user_; }
    QLabel* no_user_presets() const { return no_user_; }
    QLabel* state() const { return state_; }
    QLabel* error() const { return error_; }
    QPushButton* duplicate_button() const { return duplicate_; }
    QPushButton* export_button() const { return export_; }
    QPushButton* delete_button() const { return delete_; }
    QPushButton* reveal_button() const { return reveal_button_; }
    // A preset's row, by id; null when it is not listed.
    QListWidgetItem* row(const QString& id) const;
    // The row's menu; the caller owns it.
    class QMenu* row_menu(const PresetListing& preset);

    void duplicate(const PresetListing& source);
    void export_preset(const PresetListing& preset);
    void remove(const PresetListing& preset);
    void reveal(const PresetListing& preset);

Q_SIGNALS:
    // The library changed: a preset added or deleted.
    void changed();

private:
    std::optional<PresetListing> selected() const;
    void fill(QListWidget* list, const std::vector<PresetListing>& presets);
    void update_buttons();
    void show_error(const QString& text);

    PresetLibraryHost host_;
    AskName ask_name_;
    AskFilepath ask_filepath_;
    Confirm confirm_delete_;
    Reveal reveal_;
    std::vector<PresetListing> presets_;
    std::uint64_t generation_ = 0;

    QStackedWidget* pages_;
    QLabel* state_;
    QListWidget* built_in_;
    QListWidget* user_;
    QLabel* no_user_;
    QPushButton* duplicate_;
    QPushButton* export_;
    QPushButton* delete_;
    QPushButton* reveal_button_;
    QLabel* error_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MACHINES_PAGE_HPP

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

#ifndef BEEBIUM_LINUX_APP_KEYBOARD_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_KEYBOARD_SIDEBAR_HPP

#include "app/MappingCatalogue.hpp"

#include <QWidget>

#include <string>
#include <utility>
#include <vector>

class QAbstractItemView;
class QCheckBox;
class QFrame;
class QListWidget;
class QTreeWidget;
class QVBoxLayout;

namespace beebium::linux_client {

// The Keyboard mode (client specification 7.3, 7.4), after the macOS
// client's: the mapping chooser, the built-ins then the user's files, a file
// that would not load listed but disabled with the reason in its tooltip;
// the active mapping's labelled keys (#234), built-ins included; the Caps
// Lock synchronise toggle. The user's files are read again each time the
// mode is shown; managing their folder belongs to the settings. This
// client's own settings, so never stale.
class KeyboardSidebar : public QWidget {
    Q_OBJECT

public:
    explicit KeyboardSidebar(QString user_dirpath = user_mappings_dirpath(),
                             QWidget* parent = nullptr);

    // The mapping in use, by id, and whether Caps Lock is synchronised.
    void set_active(const QString& id);
    void set_caps_lock_sync(bool on);
    // The keys the active mapping lets the user switch off, by BBC key
    // name, and whether each is off: a "Disable <key>" box each.
    void set_key_switches(const std::vector<std::pair<std::string, bool>>& keys);
    QCheckBox* key_switch(const QString& key) const;
    // Read the user's files again.
    void rescan();

    const std::vector<MappingChoice>& choices() const { return choices_; }
    QListWidget* chooser() const { return list_; }
    QTreeWidget* reference() const { return reference_; }
    QCheckBox* caps_lock_sync() const { return caps_; }

Q_SIGNALS:
    void mapping_chosen(const beebium::linux_client::MappingChoice& choice);
    void caps_lock_sync_toggled(bool on);
    void key_switch_toggled(const QString& key, bool off);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void show_reference();
    // Place the mappings and the labelled keys in their shared area.
    void apportion();
    // The height of a view's first `rows` rows, or of all of them.
    static int rows_height(const QAbstractItemView* view, int rows);
    static int rows_height(const QAbstractItemView* view);

    QString user_dirpath_;
    std::vector<MappingChoice> choices_;
    QString active_id_;
    QWidget* views_;
    QListWidget* list_;
    QFrame* divider_;
    QTreeWidget* reference_;
    QCheckBox* caps_;
    QWidget* switches_;
    QVBoxLayout* switches_layout_;
    std::vector<std::pair<QString, QCheckBox*>> key_boxes_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_KEYBOARD_SIDEBAR_HPP

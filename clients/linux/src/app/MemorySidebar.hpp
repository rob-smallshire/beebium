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

#ifndef BEEBIUM_LINUX_APP_MEMORY_SIDEBAR_HPP
#define BEEBIUM_LINUX_APP_MEMORY_SIDEBAR_HPP

#include "connection/SidewaysTopology.hpp"

#include <QWidget>

#include <functional>

#include <optional>
#include <string>
#include <vector>

class QLabel;
class QMenu;
class QToolButton;
class QVBoxLayout;

namespace beebium::linux_client {

class SessionSideways;

// One socket, as the macOS client's Memory mode lists it: its boot
// priority, its ROM's title and version (else its file's name), a padlock
// where a write switch covers sideways RAM, its status, and an actions
// menu. The socket's label, slots, kinds and file are its tooltip. The OS
// ROM's row is one too, without a slot number.
class MemoryRow : public QWidget {
    Q_OBJECT

public:
    // Whether the server shares this host's filesystem (3.2).
    using Shares = std::function<bool()>;

    MemoryRow(SessionSideways& sideways, const SidewaysSocket& socket, Shares shares,
              QWidget* parent);
    // The OS ROM's row: its title, ROM, and the sockets' actions on its
    // image; its path is its tooltip only where the server shares this
    // host's filesystem.
    static MemoryRow* for_os_rom(SessionSideways& sideways, const std::string& title,
                                 const std::string& image_filepath, Shares shares, QWidget* parent);

    QString priority_text() const;
    QString name_text() const;
    QString status_text() const;
    // Always there, to keep the columns; hidden where there is no switch.
    QToolButton* padlock() const { return padlock_; }
    // The actions menu's button, without a tooltip of its own (11).
    QToolButton* actions() const { return actions_; }
    // Whether the padlock shows the server's write-protect engaged.
    bool shows_protected() const { return protected_; }
    // Whether the row has anything in its menu.
    bool actionable() const;
    // The row's actions, for the menu button and the context menu. The
    // caller owns it: it has no parent, as the row may be rebuilt first.
    QMenu* make_menu();

private:
    enum class Role { socket, os_rom };
    MemoryRow(SessionSideways& sideways, const SidewaysSocket& socket, Shares shares, Role role,
              QWidget* parent);

    SessionSideways& sideways_;
    SidewaysSocket socket_;
    Shares shares_;
    // Copies, as the session replaces its topology on each read-back.
    std::optional<SidewaysProtectionGroup> write_group_;
    std::optional<SidewaysProtectionGroup> hide_group_;
    bool protected_ = false;
    QLabel* priority_;
    QLabel* name_;
    QLabel* status_;
    QToolButton* padlock_;
    QToolButton* actions_;
};

// The Memory mode (client specification 11, 13.1): the machine's OS ROM,
// then its sideways sockets, highest boot priority first, read-only but
// for the protection switches. It watches the sideways memory while it shows.
class MemorySidebar : public QWidget {
    Q_OBJECT

public:
    explicit MemorySidebar(SessionSideways& sideways, MemoryRow::Shares shares = {},
                           QWidget* parent = nullptr);

    int row_count() const { return static_cast<int>(rows_.size()); }
    MemoryRow* row(int index) const { return rows_.at(static_cast<std::size_t>(index)); }
    // The OS ROM's row, there once the server has said it, else null.
    MemoryRow* os_row() const { return os_row_; }

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void rebuild();

    SessionSideways& sideways_;
    MemoryRow::Shares shares_;
    QLabel* status_;
    QLabel* os_header_;
    QWidget* os_box_;
    QVBoxLayout* os_layout_;
    MemoryRow* os_row_ = nullptr;
    QLabel* header_;
    QWidget* rows_box_;
    QVBoxLayout* rows_layout_;
    std::vector<MemoryRow*> rows_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_MEMORY_SIDEBAR_HPP

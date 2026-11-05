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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_MEMORY_SECTION_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_MEMORY_SECTION_HPP

#include "app/NewMachineDialog.hpp"
#include "preset/SidewaysDraft.hpp"

#include <memory>
#include <vector>

class QComboBox;
class QLabel;
class QMenu;
class QToolButton;
class QVBoxLayout;

namespace beebium::linux_client {

class DiagnosticLine;

// The configuration editor's Memory section (client specification 5.2,
// 11), as the macOS client's: the sideways sockets highest priority first,
// each with its image's title, its slots and the ROM's roles, a kind, a
// padlock where the socket's RAM can be write-protected, and a menu.
class MemorySection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit MemorySection(NewMachineDialog& dialog);

    SidebarMode mode() const override { return SidebarMode::memory; }
    bool changed() const override { return dialog_.draft().changed("/sideways_bank"); }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;

    int row_count() const { return static_cast<int>(rows_.size()); }
    QLabel* row_label(int row) const { return at(row).label; }
    QLabel* row_title(int row) const { return at(row).title; }
    QLabel* row_caption(int row) const { return at(row).caption; }
    QComboBox* row_kind(int row) const { return at(row).kind; }
    QToolButton* row_padlock(int row) const { return at(row).padlock; }
    // The row's menu, as its menu button opens it; the caller owns it.
    QMenu* row_menu(int row);
    QLabel* aliasing_note() const { return aliasing_; }

private:
    struct Row {
        QWidget* widget = nullptr;
        QLabel* label = nullptr;
        QLabel* title = nullptr;
        QLabel* caption = nullptr;
        QComboBox* kind = nullptr;
        QToolButton* padlock = nullptr;
        QToolButton* actions = nullptr;
        DiagnosticLine* diagnostic = nullptr;
    };
    const Row& at(int row) const { return rows_.at(static_cast<std::size_t>(row)); }
    void describe();
    void set(int row, const SocketContent& content);
    void browse(int row);

    NewMachineDialog& dialog_;
    std::unique_ptr<SidewaysDraft> sideways_;
    QLabel* heading_;
    QLabel* aliasing_;
    QVBoxLayout* rows_layout_;
    std::vector<Row> rows_;
    DiagnosticLine* diagnostic_;
    std::vector<PresetDiagnostic> diagnostics_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_MEMORY_SECTION_HPP

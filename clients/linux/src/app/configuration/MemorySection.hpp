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
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace beebium::linux_client {

class DiagnosticLine;

// The configuration editor's Memory section (client specification 5.2,
// 5.8, 11), as the macOS client's: the MOS ROM first, a name found on the
// ROM search path or a file chosen as a sideways image is, with Use Default
// returning to the preset's; then the sideways sockets highest priority first,
// each with its image's title, its slots and the ROM's roles, a kind, a
// padlock where the socket's RAM can be write-protected, and a menu. A
// socket a cartridge provides reads "No cartridge", with nothing to load
// and the way to its slot under Peripherals, until one is fitted there; then
// it is an ordinary socket naming its cartridge (5.9).
class MemorySection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit MemorySection(NewMachineDialog& dialog);

    SidebarMode mode() const override { return SidebarMode::memory; }
    // The MOS ROM and the sideways banks.
    bool changed() const override {
        return dialog_.draft().changed("/mos_image_uri") || dialog_.draft().changed("/sideways_bank");
    }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;
    // A bank's row, by its slot number.
    void reveal(const std::string& target) override;

    int row_count() const { return static_cast<int>(rows_.size()); }
    QLabel* row_label(int row) const { return at(row).label; }
    QLabel* row_title(int row) const { return at(row).title; }
    QLabel* row_caption(int row) const { return at(row).caption; }
    QComboBox* row_kind(int row) const { return at(row).kind; }
    QToolButton* row_padlock(int row) const { return at(row).padlock; }
    // The way to the slot of a cartridge that provides the socket.
    QPushButton* row_cartridge(int row) const { return at(row).cartridge; }
    // The row of the socket that answers at a slot; -1 for none.
    int row_at_slot(int slot) const;
    // The row last revealed from another section; -1 for none.
    int revealed_row() const { return revealed_; }
    // The row's menu, as its menu button opens it; the caller owns it.
    QMenu* row_menu(int row);
    QLabel* aliasing_note() const { return aliasing_; }
    QLabel* mos_title() const { return mos_title_; }
    QLabel* mos_caption() const { return mos_caption_; }
    QPushButton* mos_browse() const { return mos_browse_; }
    QPushButton* mos_default() const { return mos_default_; }

private:
    struct Row {
        QWidget* widget = nullptr;
        QLabel* label = nullptr;
        QLabel* title = nullptr;
        QLabel* caption = nullptr;
        QComboBox* kind = nullptr;
        QToolButton* padlock = nullptr;
        QToolButton* actions = nullptr;
        QPushButton* cartridge = nullptr;
        DiagnosticLine* diagnostic = nullptr;
        DraftSocket fit;  // the socket as the draft's cartridges make it
    };
    const Row& at(int row) const { return rows_.at(static_cast<std::size_t>(row)); }
    void describe();
    // The draft's cartridges, by slot point: a change rebuilds the rows.
    std::vector<std::string> fits() const;
    void set(int row, const SocketContent& content);
    void browse(int row);
    void show_mos();
    void browse_mos();
    void set_mos(const std::string& reference);
    // The model's MOS, as the schema gives it; empty without one.
    std::string default_mos() const;

    NewMachineDialog& dialog_;
    std::unique_ptr<SidewaysDraft> sideways_;
    QWidget* mos_;
    QLabel* mos_heading_;
    QLabel* mos_title_;
    QLabel* mos_caption_;
    QPushButton* mos_browse_;
    QPushButton* mos_default_;
    DiagnosticLine* mos_diagnostic_;
    QWidget* mos_rule_;
    QLabel* heading_;
    QLabel* aliasing_;
    QVBoxLayout* rows_layout_;
    std::vector<Row> rows_;
    std::vector<std::string> fits_;
    int revealed_ = -1;
    DiagnosticLine* diagnostic_;
    std::vector<PresetDiagnostic> diagnostics_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_MEMORY_SECTION_HPP

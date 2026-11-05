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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_KEYBOARD_SECTION_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_KEYBOARD_SECTION_HPP

#include "app/NewMachineDialog.hpp"
#include "preset/StartupLinks.hpp"

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QVBoxLayout;

namespace beebium::linux_client {

class DiagnosticLine;

// The configuration editor's Keyboard section (client specification 5.2),
// as the macOS client's: the startup links, one byte seen four ways, as a
// screen mode and auto-boot, a box per link, and the byte in hex and in
// decimal. Changing any redraws the rest.
class KeyboardSection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit KeyboardSection(NewMachineDialog& dialog);

    SidebarMode mode() const override { return SidebarMode::keyboard; }
    std::vector<std::string> pointers() const override { return {"/startup"}; }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;

    QComboBox* screen_mode() const { return screen_mode_; }
    QCheckBox* auto_boot() const { return auto_boot_; }
    // The box for a link, by the bit it reads; null when there is none.
    QCheckBox* link(int bit) const;
    QLineEdit* hex() const { return hex_; }
    QLineEdit* decimal() const { return decimal_; }

private:
    StartupLinks links() const;
    void write(const StartupLinks& links);
    void show_links();

    NewMachineDialog& dialog_;
    std::optional<LinksLayout> layout_;
    QComboBox* screen_mode_;
    QCheckBox* auto_boot_;
    class QLabel* auto_boot_description_;
    QVBoxLayout* links_layout_;
    std::vector<std::pair<int, QCheckBox*>> link_boxes_;
    std::vector<QWidget*> link_rows_;
    QLineEdit* hex_;
    QLineEdit* decimal_;
    DiagnosticLine* diagnostic_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_KEYBOARD_SECTION_HPP

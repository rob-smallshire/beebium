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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_STORAGE_SECTION_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_STORAGE_SECTION_HPP

#include "app/NewMachineDialog.hpp"

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace beebium::linux_client {

class DiagnosticLine;

// The configuration editor's Storage section (client specification 5.2,
// 9), as the macOS client's: the disc controller in its socket, from the
// schema's options, and a floppy drive's disc for each drive while a
// controller is fitted. Images are written as file:// URIs.
class StorageSection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit StorageSection(NewMachineDialog& dialog);

    SidebarMode mode() const override { return SidebarMode::storage; }
    std::vector<std::string> pointers() const override { return {"/storage"}; }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;

    QComboBox* controller() const { return controller_; }
    int drive_count() const { return static_cast<int>(drives_.size()); }
    QLineEdit* drive_field(int drive) const { return drives_.at(static_cast<std::size_t>(drive)).field; }
    QPushButton* drive_browse(int drive) const {
        return drives_.at(static_cast<std::size_t>(drive)).browse;
    }

private:
    struct Drive {
        QWidget* row = nullptr;
        QLineEdit* field = nullptr;
        QPushButton* browse = nullptr;
        DiagnosticLine* diagnostic = nullptr;
    };
    bool controller_fitted() const;
    void set_image(int drive, const QString& path);

    NewMachineDialog& dialog_;
    QWidget* controller_row_;
    QComboBox* controller_;
    QLabel* controller_description_;
    DiagnosticLine* controller_diagnostic_;
    QVBoxLayout* drives_layout_;
    std::vector<Drive> drives_;
    std::vector<SchemaFdcOption> options_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_STORAGE_SECTION_HPP

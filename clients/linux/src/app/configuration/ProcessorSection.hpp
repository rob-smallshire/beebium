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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_PROCESSOR_SECTION_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_PROCESSOR_SECTION_HPP

#include "app/NewMachineDialog.hpp"
#include "preset/ExtensionsDraft.hpp"

#include <memory>
#include <vector>

class QComboBox;
class QVBoxLayout;

namespace beebium::linux_client {

class AttachmentPointGroup;
class DiagnosticLine;

// The configuration editor's Processor section (client specification
// 5.2), as the macOS client's: the Tube's coprocessor, where the model has
// a Tube, as the Peripherals section's points are, then the emulation
// speed.
class ProcessorSection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit ProcessorSection(NewMachineDialog& dialog);

    SidebarMode mode() const override { return SidebarMode::processor; }
    std::vector<std::string> pointers() const override { return {"/speed"}; }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;

    AttachmentPointGroup* tube() const { return tube_; }
    QComboBox* speed() const { return speed_; }

private:
    void show_speed();

    NewMachineDialog& dialog_;
    std::unique_ptr<ExtensionsDraft> extensions_;
    QVBoxLayout* tube_layout_;
    AttachmentPointGroup* tube_ = nullptr;
    QWidget* tube_rule_;
    QComboBox* speed_;
    DiagnosticLine* diagnostic_;
    std::vector<PresetDiagnostic> diagnostics_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_PROCESSOR_SECTION_HPP

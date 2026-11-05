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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_PERIPHERALS_SECTION_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_PERIPHERALS_SECTION_HPP

#include "app/NewMachineDialog.hpp"
#include "preset/ExtensionsDraft.hpp"

#include <memory>
#include <vector>

class QVBoxLayout;

namespace beebium::linux_client {

class AttachmentPointGroup;
class DiagnosticLine;

// The configuration editor's Peripherals section (client specification
// 5.2), as the macOS client's: a group for each of the model's own
// attachment points but the Tube, which the Processor section holds.
class PeripheralsSection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit PeripheralsSection(NewMachineDialog& dialog);

    // Whether a schema gives the section anything to show.
    static bool applies(const PresetSchema& schema);

    SidebarMode mode() const override { return SidebarMode::peripherals; }
    std::vector<std::string> pointers() const override { return {"/extensions"}; }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;

    AttachmentPointGroup* group(const std::string& point) const;
    // What no point places; null when everything is placed.
    AttachmentPointGroup* unattached() const { return unattached_; }

private:
    NewMachineDialog& dialog_;
    std::unique_ptr<ExtensionsDraft> extensions_;
    QWidget* content_ = nullptr;
    QVBoxLayout* content_layout_ = nullptr;
    std::vector<AttachmentPointGroup*> groups_;
    AttachmentPointGroup* unattached_ = nullptr;
    DiagnosticLine* diagnostic_ = nullptr;
    std::vector<PresetDiagnostic> diagnostics_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_PERIPHERALS_SECTION_HPP

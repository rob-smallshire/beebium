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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_NETWORK_SECTION_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_NETWORK_SECTION_HPP

#include "app/NewMachineDialog.hpp"
#include "preset/EconetStation.hpp"

#include <vector>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QVBoxLayout;

namespace beebium::linux_client {

class DiagnosticLine;
class ParameterForm;

// The configuration editor's Network section (client specification 5.2),
// as the macOS client's: Econet on or off, its station as a number or
// chosen at launch, and the transport it is connected to, or none, with
// the transport's parameters inline. Econet off leaves the rest disabled.
class NetworkSection : public ConfigurationSection {
    Q_OBJECT

public:
    explicit NetworkSection(NewMachineDialog& dialog);

    SidebarMode mode() const override { return SidebarMode::network; }
    std::vector<std::string> pointers() const override { return {"/econet"}; }
    void reload() override;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) override;

    QCheckBox* econet() const { return econet_; }
    QRadioButton* station_mode(EconetStation::Mode mode) const;
    QLineEdit* station_number() const { return number_; }
    QLineEdit* station_low() const { return low_; }
    QLineEdit* station_high() const { return high_; }
    QLabel* transport_name() const { return transport_name_; }
    QPushButton* change_transport() const { return change_; }
    ParameterForm* transport_form() const { return form_; }

private:
    bool on() const;
    EconetStation station() const;
    void write_station(const EconetStation& station);
    void choose_transport();
    void show_station();
    void show_transport();

    NewMachineDialog& dialog_;
    SchemaStation bounds_;
    QCheckBox* econet_;
    QWidget* body_;
    QButtonGroup* modes_;
    QStackedWidget* mode_pages_;
    QLineEdit* number_;
    QLabel* bounds_label_;
    QLineEdit* low_;
    QLineEdit* high_;
    DiagnosticLine* station_diagnostic_;
    QLabel* transport_name_;
    QLabel* transport_identifier_;
    QPushButton* change_;
    QLabel* transport_description_;
    QVBoxLayout* form_layout_;
    ParameterForm* form_ = nullptr;
    DiagnosticLine* transport_diagnostic_;
    DiagnosticLine* econet_diagnostic_;
    std::vector<PresetDiagnostic> diagnostics_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_NETWORK_SECTION_HPP

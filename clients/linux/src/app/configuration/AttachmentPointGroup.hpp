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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_ATTACHMENT_POINT_GROUP_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_ATTACHMENT_POINT_GROUP_HPP

#include "preset/ExtensionsDraft.hpp"
#include "preset/PresetResolution.hpp"

#include <QWidget>

#include <optional>
#include <vector>

class QLabel;
class QPushButton;
class QToolButton;

namespace beebium::linux_client {

class NewMachineDialog;
class ParameterForm;

// One attachment point of the configuration editor (client specification
// 5.2), as the macOS client's: the point's name, occupancy, an Add while
// there is room, its description, and an instance card for each extension
// attached, its parameters inline and the points it provides beneath it.
class AttachmentPointGroup : public QWidget {
    Q_OBJECT

public:
    AttachmentPointGroup(NewMachineDialog& dialog, ExtensionsDraft& extensions,
                         SchemaAttachmentPoint point,
                         std::optional<ExtensionInstance> provider = std::nullopt,
                         QWidget* parent = nullptr);
    // What no point places, for the user to see and remove: no occupancy,
    // Add or Change.
    static AttachmentPointGroup* unattached(NewMachineDialog& dialog, ExtensionsDraft& extensions,
                                            QWidget* parent = nullptr);

    const SchemaAttachmentPoint& point() const { return point_; }
    QLabel* occupancy() const { return occupancy_; }
    QPushButton* add_button() const { return add_; }
    // Every form beneath this point, its nested points' included.
    std::vector<ParameterForm*> forms() const;
    void set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics);

    // An instance's card, by its framework id; null when not attached here.
    QWidget* card(const std::string& id) const;
    QPushButton* change_button(const std::string& id) const;
    QToolButton* remove_button(const std::string& id) const;
    AttachmentPointGroup* nested(const std::string& id, const std::string& point) const;

Q_SIGNALS:
    // An extension added, removed or changed: the tree is redrawn.
    void restructured();
    // A parameter changed through a form.
    void edited();

private:
    AttachmentPointGroup(NewMachineDialog& dialog, ExtensionsDraft& extensions,
                         SchemaAttachmentPoint point, std::optional<ExtensionInstance> provider,
                         std::optional<std::vector<ExtensionInstance>> fixed, QWidget* parent);

    struct Card {
        ExtensionInstance instance;
        QWidget* widget = nullptr;
        QPushButton* change = nullptr;
        QToolButton* remove = nullptr;
        ParameterForm* form = nullptr;
        class DiagnosticLine* diagnostic = nullptr;  // the instance's own
        std::vector<AttachmentPointGroup*> nested;
    };
    const ExtensionInstance* provider() const { return provider_ ? &*provider_ : nullptr; }
    void add();
    void change(const ExtensionInstance& instance);

    NewMachineDialog& dialog_;
    ExtensionsDraft& extensions_;
    SchemaAttachmentPoint point_;
    std::optional<ExtensionInstance> provider_;
    QLabel* occupancy_;
    QPushButton* add_ = nullptr;
    std::vector<Card> cards_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_ATTACHMENT_POINT_GROUP_HPP

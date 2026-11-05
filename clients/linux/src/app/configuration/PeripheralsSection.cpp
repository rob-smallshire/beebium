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

#include "app/configuration/PeripheralsSection.hpp"

#include "app/configuration/AttachmentPointGroup.hpp"
#include "app/configuration/SectionWidgets.hpp"

#include <QFrame>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

namespace {

const std::vector<std::string> PROCESSOR_POINTS{"tube"};

}  // namespace

bool PeripheralsSection::applies(const PresetSchema& schema) {
    for (const auto& point : schema.attachment_points()) {
        if (!point.provided_by && !point.extensions.empty() &&
            std::find(PROCESSOR_POINTS.begin(), PROCESSOR_POINTS.end(), point.id) ==
                PROCESSOR_POINTS.end()) {
            return true;
        }
    }
    return false;
}

PeripheralsSection::PeripheralsSection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("peripheralsSection"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    layout->addWidget(scroll);
    auto* inner = new QWidget(scroll);
    auto* inner_layout = new QVBoxLayout(inner);
    content_layout_ = new QVBoxLayout;
    inner_layout->addLayout(content_layout_);
    diagnostic_ = new DiagnosticLine(inner);
    inner_layout->addWidget(diagnostic_);
    inner_layout->addStretch(1);
    scroll->setWidget(inner);
}

void PeripheralsSection::reload() {
    for (auto* group : groups_) {
        group->hide();
        group->deleteLater();
    }
    groups_.clear();
    unattached_ = nullptr;
    const auto& schema = dialog_.schema();
    if (!schema) {
        return;
    }
    extensions_ = std::make_unique<ExtensionsDraft>(dialog_.draft(), *schema);
    std::vector<AttachmentPointGroup*> made;
    for (const auto& point : extensions_->model_points(PROCESSOR_POINTS)) {
        made.push_back(new AttachmentPointGroup(dialog_, *extensions_, point, std::nullopt, this));
    }
    if (!extensions_->unattached().empty()) {
        unattached_ = AttachmentPointGroup::unattached(dialog_, *extensions_, this);
        made.push_back(unattached_);
    }
    for (auto* group : made) {
        // Redrawn once the button that asked has finished.
        connect(group, &AttachmentPointGroup::restructured, this, [this] {
            QTimer::singleShot(0, this, [this] {
                reload();
                Q_EMIT edited();
            });
        });
        connect(group, &AttachmentPointGroup::edited, this, &ConfigurationSection::edited);
        content_layout_->addWidget(group);
        groups_.push_back(group);
    }
    set_diagnostics(diagnostics_);
}

void PeripheralsSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    for (auto* group : groups_) {
        group->set_diagnostics(diagnostics);
    }
    if (!extensions_) {
        return;
    }
    // Every instance has a card, here, in the Processor section or
    // unattached; the list's own diagnostics go at the foot.
    std::vector<PresetDiagnostic> unplaced;
    for (const auto& d : diagnostics) {
        if (d.path == "/extensions") {
            unplaced.push_back(d);
        }
    }
    diagnostic_->show_for(unplaced, "/extensions");
}

bool PeripheralsSection::changed() const {
    const auto tube = point_extensions(dialog_.schema(), PROCESSOR_POINTS.front());
    return dialog_.draft().changed_where(
        "/extensions", [&](const nlohmann::ordered_json& entry) { return !names_one_of(entry, tube); });
}

AttachmentPointGroup* PeripheralsSection::group(const std::string& point) const {
    for (auto* g : groups_) {
        if (g->point().id == point) {
            return g;
        }
    }
    return nullptr;
}

}  // namespace beebium::linux_client

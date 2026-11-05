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

#include "app/configuration/ProcessorSection.hpp"

#include "app/SpeedScale.hpp"
#include "app/configuration/AttachmentPointGroup.hpp"
#include "app/configuration/SectionWidgets.hpp"

#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

const char* TUBE = "tube";
const double MULTIPLIERS[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};

}  // namespace

ProcessorSection::ProcessorSection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("processorSection"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* inner = new QWidget(scroll);
    auto* layout = new QVBoxLayout(inner);
    tube_layout_ = new QVBoxLayout;
    layout->addLayout(tube_layout_);
    auto* rule = new QFrame(inner);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    tube_rule_ = rule;
    layout->addWidget(rule);
    layout->addWidget(section_heading(tr("Emulation Speed"), inner));
    speed_ = new QComboBox(inner);
    speed_->setObjectName(QStringLiteral("emulationSpeed"));
    speed_->setMaximumWidth(220);
    layout->addWidget(speed_);
    diagnostic_ = new DiagnosticLine(inner);
    layout->addWidget(diagnostic_);
    layout->addStretch(1);
    scroll->setWidget(inner);

    connect(speed_, &QComboBox::activated, this, [this](int index) {
        const QVariant value = speed_->itemData(index);
        const ordered_json speed = value.isValid() ? ordered_json(value.toDouble())
                                                   : ordered_json("unlimited");
        if (dialog_.draft().get("/speed") == speed) {
            return;
        }
        dialog_.draft().set("/speed", speed);
        Q_EMIT edited();
    });
}

void ProcessorSection::reload() {
    if (tube_) {
        tube_->hide();
        tube_->deleteLater();
        tube_ = nullptr;
    }
    const auto& schema = dialog_.schema();
    if (schema) {
        extensions_ = std::make_unique<ExtensionsDraft>(dialog_.draft(), *schema);
        for (const auto& point : schema->attachment_points()) {
            if (point.id == TUBE && !point.provided_by) {
                tube_ = new AttachmentPointGroup(dialog_, *extensions_, point, std::nullopt, this);
                connect(tube_, &AttachmentPointGroup::restructured, this, [this] {
                    QTimer::singleShot(0, this, [this] {
                        reload();
                        Q_EMIT edited();
                    });
                });
                connect(tube_, &AttachmentPointGroup::edited, this, &ConfigurationSection::edited);
                tube_layout_->addWidget(tube_);
            }
        }
    }
    tube_rule_->setVisible(tube_ != nullptr);
    show_speed();
    set_diagnostics(diagnostics_);
}

void ProcessorSection::show_speed() {
    const QSignalBlocker quiet(speed_);
    speed_->clear();
    const auto speed = dialog_.draft().get("/speed");
    std::vector<double> multipliers(std::begin(MULTIPLIERS), std::end(MULTIPLIERS));
    // A preset's own multiplier is kept, so it goes back as it came.
    if (speed && speed->is_number() && speed->get<double>() > 0.0 &&
        std::find(multipliers.begin(), multipliers.end(), speed->get<double>()) == multipliers.end()) {
        multipliers.push_back(speed->get<double>());
        std::sort(multipliers.begin(), multipliers.end());
    }
    for (double multiplier : multipliers) {
        speed_->addItem(speed_label(multiplier), multiplier);
        if (speed && speed->is_number() && speed->get<double>() == multiplier) {
            speed_->setCurrentIndex(speed_->count() - 1);
        }
    }
    speed_->addItem(tr("Unlimited"));
    if (speed && speed->is_string()) {
        speed_->setCurrentIndex(speed_->count() - 1);
    }
}

bool ProcessorSection::changed() const {
    const auto tube = point_extensions(dialog_.schema(), TUBE);
    return dialog_.draft().changed("/speed") ||
           dialog_.draft().changed_where("/extensions",
                                         [&](const ordered_json& entry) { return names_one_of(entry, tube); });
}

void ProcessorSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    if (tube_) {
        tube_->set_diagnostics(diagnostics);
    }
    diagnostic_->show_for(diagnostics, "/speed");
}

}  // namespace beebium::linux_client

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
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

using nlohmann::ordered_json;

namespace {

// The model's Tube points, in the order they are shown.
const std::vector<std::string> TUBES{"tube", "internal-tube"};
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

    mos_ = new QWidget(inner);
    auto* mos_layout = new QVBoxLayout(mos_);
    mos_layout->setContentsMargins(0, 8, 0, 0);
    mos_heading_ = section_heading(QString(), mos_);
    mos_layout->addWidget(mos_heading_);
    auto* mos_line = new QHBoxLayout;
    auto* mos_text = new QVBoxLayout;
    mos_title_ = new QLabel(mos_);
    mos_title_->setObjectName(QStringLiteral("mosImage"));
    mos_caption_ = section_caption(mos_);
    mos_text->addWidget(mos_title_);
    mos_text->addWidget(mos_caption_);
    mos_line->addLayout(mos_text, 1);
    mos_browse_ = new QPushButton(tr("Browse..."), mos_);
    mos_browse_->setObjectName(QStringLiteral("mosBrowse"));
    mos_line->addWidget(mos_browse_);
    mos_default_ = new QPushButton(tr("Use Default"), mos_);
    mos_default_->setObjectName(QStringLiteral("mosDefault"));
    mos_line->addWidget(mos_default_);
    mos_layout->addLayout(mos_line);
    mos_diagnostic_ = new DiagnosticLine(mos_);
    mos_layout->addWidget(mos_diagnostic_);
    layout->addWidget(mos_);
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
    connect(mos_browse_, &QPushButton::clicked, this, [this] { browse_mos(); });
    connect(mos_default_, &QPushButton::clicked, this, [this] { set_mos(default_mos()); });
}

AttachmentPointGroup* ProcessorSection::tube(const std::string& point) const {
    for (AttachmentPointGroup* group : tubes_) {
        if (group->point().id == point) {
            return group;
        }
    }
    return nullptr;
}

void ProcessorSection::reload() {
    for (AttachmentPointGroup* group : tubes_) {
        group->hide();
        group->deleteLater();
    }
    tubes_.clear();
    const auto& schema = dialog_.schema();
    if (schema) {
        extensions_ = std::make_unique<ExtensionsDraft>(dialog_.draft(), *schema);
        for (const std::string& id : TUBES) {
            for (const auto& point : schema->attachment_points()) {
                if (point.id != id || point.provided_by) {
                    continue;
                }
                auto* group = new AttachmentPointGroup(dialog_, *extensions_, point, std::nullopt, this);
                connect(group, &AttachmentPointGroup::restructured, this, [this] {
                    QTimer::singleShot(0, this, [this] {
                        reload();
                        Q_EMIT edited();
                    });
                });
                connect(group, &AttachmentPointGroup::edited, this, &ConfigurationSection::edited);
                tube_layout_->addWidget(group);
                tubes_.push_back(group);
            }
        }
    }
    tube_rule_->setVisible(!tubes_.empty());
    show_speed();
    show_mos();
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

std::string ProcessorSection::default_mos() const {
    const auto& schema = dialog_.schema();
    const ordered_json* section = schema ? schema->section("mos_image_uri") : nullptr;
    return section ? section->value("default", std::string()) : std::string();
}

void ProcessorSection::show_mos() {
    const auto& schema = dialog_.schema();
    const bool shown = schema && schema->section("mos_image_uri");
    mos_->setVisible(shown);
    if (!shown) {
        return;
    }
    mos_heading_->setText(QString::fromStdString(schema->section_label("mos_image_uri")));
    const auto value = dialog_.draft().get("/mos_image_uri");
    const std::string reference = value && value->is_string() ? value->get<std::string>() : std::string();
    const std::string model_default = default_mos();
    // The server reads the MOS as a name on the ROM search path or a path.
    const QString path = QDir::isAbsolutePath(QString::fromStdString(reference))
                             ? QString::fromStdString(reference)
                             : QString();
    if (reference.empty()) {
        mos_title_->setText(tr("The model's MOS"));
        mos_caption_->setText(QString::fromStdString(model_default));
    } else if (!path.isEmpty()) {
        mos_title_->setText(QFileInfo(path).fileName());
        mos_caption_->setText(path);
    } else {
        mos_title_->setText(QString::fromStdString(reference));
        mos_caption_->setText(reference == model_default ? tr("The model's MOS, from the ROM search path")
                                                         : tr("From the ROM search path"));
    }
    mos_default_->setEnabled(!model_default.empty() && !reference.empty() && reference != model_default);
    mos_default_->setToolTip(QString::fromStdString(model_default));
}

void ProcessorSection::browse_mos() {
    FormBrowse request;
    request.key = "mos-image";
    request.chooser = FormBrowse::Chooser::open_file;
    request.label = dialog_.schema() ? dialog_.schema()->section_label("mos_image_uri") : "MOS ROM";
    const auto value = dialog_.draft().get("/mos_image_uri");
    if (value && value->is_string() && QDir::isAbsolutePath(QString::fromStdString(value->get<std::string>()))) {
        request.current = value->get<std::string>();
    }
    const auto path = dialog_.choose(request);
    if (!path || path->isEmpty()) {
        return;
    }
    // A path, as a sideways image's is: the server reads either.
    set_mos(QFileInfo(*path).absoluteFilePath().toStdString());
}

void ProcessorSection::set_mos(const std::string& reference) {
    if (reference.empty() || dialog_.draft().get("/mos_image_uri") == ordered_json(reference)) {
        return;
    }
    dialog_.draft().set("/mos_image_uri", reference);
    show_mos();
    Q_EMIT edited();
}

bool ProcessorSection::changed() const {
    std::vector<std::string> tube;
    for (const std::string& id : TUBES) {
        for (const std::string& name : point_extensions(dialog_.schema(), id)) {
            tube.push_back(name);
        }
    }
    return dialog_.draft().changed("/speed") || dialog_.draft().changed("/mos_image_uri") ||
           dialog_.draft().changed_where("/extensions",
                                         [&](const ordered_json& entry) { return names_one_of(entry, tube); });
}

void ProcessorSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    for (AttachmentPointGroup* group : tubes_) {
        group->set_diagnostics(diagnostics);
    }
    diagnostic_->show_for(diagnostics, "/speed");
    mos_diagnostic_->show_for(diagnostics, "/mos_image_uri");
}

}  // namespace beebium::linux_client

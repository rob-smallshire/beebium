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

#include "app/configuration/StorageSection.hpp"

#include "app/configuration/SectionWidgets.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace beebium::linux_client {

using nlohmann::ordered_json;

StorageSection::StorageSection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("storageSection"));
    auto* layout = new QVBoxLayout(this);
    controller_row_ = new QWidget(this);
    auto* controller_layout = new QVBoxLayout(controller_row_);
    controller_layout->setContentsMargins(0, 0, 0, 0);
    controller_layout->addWidget(section_heading(tr("Disc Controller"), controller_row_));
    controller_ = new QComboBox(controller_row_);
    controller_->setObjectName(QStringLiteral("discController"));
    controller_layout->addWidget(controller_);
    controller_description_ = section_caption(controller_row_);
    controller_layout->addWidget(controller_description_);
    controller_diagnostic_ = new DiagnosticLine(controller_row_);
    controller_layout->addWidget(controller_diagnostic_);
    layout->addWidget(controller_row_);
    drives_layout_ = new QVBoxLayout;
    layout->addLayout(drives_layout_);
    layout->addStretch(1);

    connect(controller_, &QComboBox::activated, this, [this](int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= options_.size()) {
            return;
        }
        dialog_.draft().set("/storage/fdc_socket", ordered_json{{"id", options_[index].id}});
        reload();
        Q_EMIT edited();
    });
}

bool StorageSection::controller_fitted() const {
    const auto socket = dialog_.draft().get("/storage/fdc_socket");
    if (!socket) {
        return true;  // a controller built in
    }
    return socket->value("id", std::string("none")) != "none";
}

void StorageSection::reload() {
    const auto& schema = dialog_.schema();
    options_ = schema ? schema->fdc_options() : std::vector<SchemaFdcOption>{};
    const auto socket = dialog_.draft().get("/storage/fdc_socket");
    // A model with its controller built in has no socket to choose.
    controller_row_->setVisible(!options_.empty() && socket.has_value());
    {
        const QSignalBlocker quiet(controller_);
        controller_->clear();
        const std::string fitted = socket ? socket->value("id", std::string("none")) : std::string();
        for (std::size_t i = 0; i < options_.size(); ++i) {
            controller_->addItem(QString::fromStdString(options_[i].label));
            if (options_[i].id == fitted) {
                controller_->setCurrentIndex(static_cast<int>(i));
                controller_description_->setText(QString::fromStdString(options_[i].description));
            }
        }
    }

    for (Drive& drive : drives_) {
        drive.row->deleteLater();
    }
    drives_.clear();
    const auto floppies = dialog_.draft().get("/storage/floppy_drives");
    if (!controller_fitted() || !floppies || !floppies->is_array()) {
        return;
    }
    const ordered_json* layout_drives =
        schema && schema->section("storage") ? &(*schema->section("storage"))["floppy_drives"]
                                             : nullptr;
    for (std::size_t i = 0; i < floppies->size(); ++i) {
        Drive drive;
        drive.row = new QWidget(this);
        auto* rows = new QVBoxLayout(drive.row);
        rows->setContentsMargins(0, 8, 0, 0);
        QString label = tr("Floppy drive %1").arg(i);
        if (layout_drives && layout_drives->is_array() && i < layout_drives->size()) {
            label = QString::fromStdString((*layout_drives)[i].value("label", label.toStdString()));
        }
        rows->addWidget(section_heading(label, drive.row));
        auto* line = new QHBoxLayout;
        drive.field = new QLineEdit(drive.row);
        drive.field->setObjectName(QStringLiteral("floppy%1").arg(i));
        const auto uri = (*floppies)[i].value("image_uri", ordered_json());
        drive.field->setText(uri.is_string() ? path_of_uri(uri.get<std::string>()) : QString());
        drive.browse = new QPushButton(tr("Browse..."), drive.row);
        line->addWidget(drive.field, 1);
        line->addWidget(drive.browse);
        rows->addLayout(line);
        drive.diagnostic = new DiagnosticLine(drive.row);
        rows->addWidget(drive.diagnostic);
        drives_layout_->addWidget(drive.row);
        const int number = static_cast<int>(i);
        connect(drive.field, &QLineEdit::editingFinished, this, [this, number] {
            set_image(number, drives_[static_cast<std::size_t>(number)].field->text().trimmed());
        });
        connect(drive.browse, &QPushButton::clicked, this, [this, number, label] {
            FormBrowse browse;
            browse.chooser = FormBrowse::Chooser::open_file;
            browse.label = label.toStdString();
            browse.current = drives_[static_cast<std::size_t>(number)].field->text().toStdString();
            if (const auto path = dialog_.choose(browse)) {
                drives_[static_cast<std::size_t>(number)].field->setText(*path);
                set_image(number, *path);
            }
        });
        drives_.push_back(drive);
    }
}

void StorageSection::set_image(int drive, const QString& path) {
    auto drives = dialog_.draft().get("/storage/floppy_drives");
    if (!drives || !drives->is_array() || static_cast<std::size_t>(drive) >= drives->size()) {
        return;
    }
    ordered_json& entry = (*drives)[static_cast<std::size_t>(drive)];
    // An empty drive is null, as the server writes it.
    const ordered_json written = path.isEmpty() ? ordered_json() : ordered_json(uri_of_path(path));
    if (entry.value("image_uri", ordered_json()) == written) {
        return;
    }
    entry["image_uri"] = written;
    dialog_.draft().set("/storage/floppy_drives", *drives);
    Q_EMIT edited();
}

void StorageSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    controller_diagnostic_->show_for(diagnostics, "/storage/fdc_socket");
    for (std::size_t i = 0; i < drives_.size(); ++i) {
        drives_[i].diagnostic->show_for(diagnostics, "/storage/floppy_drives/" + std::to_string(i));
    }
}

}  // namespace beebium::linux_client

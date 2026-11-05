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
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
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

    hard_discs_ = new QWidget(this);
    auto* hard = new QVBoxLayout(hard_discs_);
    hard->setContentsMargins(0, 8, 0, 0);
    auto* rule = new QFrame(hard_discs_);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    hard->addWidget(rule);
    auto* heading = new QHBoxLayout;
    heading->addWidget(section_heading(tr("Hard discs"), hard_discs_));
    heading->addStretch(1);
    add_disc_ = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), tr("Add disc"), hard_discs_);
    add_disc_->setObjectName(QStringLiteral("addHardDisc"));
    add_disc_->setFlat(true);
    heading->addWidget(add_disc_);
    hard->addLayout(heading);
    no_discs_ = section_caption(hard_discs_);
    no_discs_->setText(tr("No hard discs."));
    hard->addWidget(no_discs_);
    discs_layout_ = new QVBoxLayout;
    hard->addLayout(discs_layout_);
    layout->addWidget(hard_discs_);
    layout->addStretch(1);

    connect(add_disc_, &QPushButton::clicked, this, [this] {
        // With several adapters the button's menu asks which.
        if (hard_disc_draft_ && !add_disc_->menu()) {
            hard_disc_draft_->add();
            disc_edited();
        }
    });

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
    const auto capability = schema ? HardDiscCapability::from_schema(*schema) : std::nullopt;
    hard_disc_draft_ = capability ? std::make_unique<HardDiscsDraft>(dialog_.draft(), *schema, *capability)
                                  : nullptr;
    hard_discs_->setVisible(capability.has_value());
    show_hard_discs();

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
        drive.clear = new QPushButton(tr("Clear"), drive.row);
        line->addWidget(drive.field, 1);
        line->addWidget(drive.browse);
        line->addWidget(drive.clear);
        rows->addLayout(line);
        drive.diagnostic = new DiagnosticLine(drive.row);
        rows->addWidget(drive.diagnostic);
        drives_layout_->addWidget(drive.row);
        const int number = static_cast<int>(i);
        connect(drive.field, &QLineEdit::editingFinished, this, [this, number] {
            set_image(number, drives_[static_cast<std::size_t>(number)].field->text().trimmed());
        });
        connect(drive.clear, &QPushButton::clicked, this, [this, number] {
            drives_[static_cast<std::size_t>(number)].field->clear();
            set_image(number, QString());
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

void StorageSection::disc_edited() {
    // Redrawn once the control that asked has finished.
    QTimer::singleShot(0, this, [this] { show_hard_discs(); });
    Q_EMIT edited();
}

void StorageSection::show_hard_discs() {
    for (DiscRow& row : discs_) {
        row.row->hide();
        row.row->deleteLater();
    }
    discs_.clear();
    if (!hard_disc_draft_) {
        return;
    }
    HardDiscsDraft& draft = *hard_disc_draft_;
    const auto adapters = draft.adapters();
    const HardDiscCapability& capability = draft.capability();
    // One adapter or none: the button adds; several: a menu of them.
    if (QMenu* old = add_disc_->menu()) {
        add_disc_->setMenu(nullptr);
        old->deleteLater();
    }
    if (adapters.size() > 1) {
        auto* menu = new QMenu(add_disc_);
        for (const auto& adapter : adapters) {
            QAction* on = menu->addAction(tr("On %1").arg(QString::fromStdString(adapter)));
            on->setEnabled(draft.can_add(adapter));
            connect(on, &QAction::triggered, this, [this, adapter] {
                hard_disc_draft_->add(adapter);
                disc_edited();
            });
        }
        add_disc_->setMenu(menu);
        add_disc_->setEnabled(true);
        add_disc_->setToolTip(QString());
    } else {
        const bool room = draft.can_add();
        add_disc_->setEnabled(room);
        add_disc_->setToolTip(room || !capability.per_adapter
                                  ? QString()
                                  : tr("This machine holds at most %1 hard discs")
                                        .arg(*capability.per_adapter));
    }

    const auto discs = draft.discs();
    no_discs_->setVisible(discs.empty());
    for (std::size_t i = 0; i < discs.size(); ++i) {
        DiscRow row;
        row.disc = discs[i];
        row.row = new QWidget(hard_discs_);
        auto* rows = new QVBoxLayout(row.row);
        rows->setContentsMargins(0, 4, 0, 4);
        auto* top = new QHBoxLayout;
        auto* id_label = new QLabel(tr("SCSI ID"), row.row);
        top->addWidget(id_label);
        row.id = new QComboBox(row.row);
        row.id->setObjectName(QStringLiteral("hardDiscId%1").arg(i));
        for (int id = capability.id_min; id <= capability.id_max; ++id) {
            row.id->addItem(QString::number(id), id);
        }
        row.id->setCurrentIndex(row.id->findData(row.disc.id));
        id_label->setBuddy(row.id);
        top->addWidget(row.id);
        row.duplicate = new QLabel(row.row);
        const QIcon warning = QIcon::fromTheme(QStringLiteral("dialog-warning"));
        if (warning.isNull()) {
            row.duplicate->setText(QStringLiteral("!"));
        } else {
            row.duplicate->setPixmap(warning.pixmap(16, 16));
        }
        row.duplicate->setToolTip(tr("Another disc uses SCSI ID %1").arg(row.disc.id));
        row.duplicate->setVisible(draft.duplicate_id(row.disc));
        top->addWidget(row.duplicate);
        if (adapters.size() > 1) {
            top->addWidget(new QLabel(tr("on"), row.row));
            row.adapter = new QComboBox(row.row);
            row.adapter->addItem(tr("Last adapter"), QString());
            for (const auto& adapter : adapters) {
                row.adapter->addItem(QString::fromStdString(adapter), QString::fromStdString(adapter));
            }
            row.adapter->setCurrentIndex(
                std::max(0, row.adapter->findData(QString::fromStdString(row.disc.adapter))));
            top->addWidget(row.adapter);
        }
        top->addStretch(1);
        row.remove = new QToolButton(row.row);
        row.remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
        if (row.remove->icon().isNull()) {
            row.remove->setText(tr("Remove"));
        }
        row.remove->setAutoRaise(true);
        row.remove->setToolTip(tr("Remove this hard disc"));
        row.remove->setAccessibleName(tr("Remove this hard disc"));
        top->addWidget(row.remove);
        rows->addLayout(top);

        auto* line = new QHBoxLayout;
        row.field = new QLineEdit(QString::fromStdString(row.disc.image), row.row);
        row.field->setObjectName(QStringLiteral("hardDisc%1").arg(i));
        row.field->setAccessibleName(tr("Hard disc image"));
        row.browse = new QPushButton(tr("Browse..."), row.row);
        row.clear = new QPushButton(tr("Clear"), row.row);
        line->addWidget(row.field, 1);
        line->addWidget(row.browse);
        line->addWidget(row.clear);
        rows->addLayout(line);
        // A bare name is a bundled master, copied for each machine.
        if (!row.disc.image.empty() && row.disc.image.find('/') == std::string::npos) {
            QLabel* copy = section_caption(row.row);
            copy->setText(tr("A per-machine working copy is used"));
            rows->addWidget(copy);
        }
        row.diagnostic = new DiagnosticLine(row.row);
        rows->addWidget(row.diagnostic);
        discs_layout_->addWidget(row.row);

        const HardDisc disc = row.disc;
        connect(row.id, &QComboBox::activated, this, [this, disc, combo = row.id](int index) {
            const int id = combo->itemData(index).toInt();
            if (id != disc.id) {
                hard_disc_draft_->set_id(disc, id);
                disc_edited();
            }
        });
        if (row.adapter) {
            connect(row.adapter, &QComboBox::activated, this, [this, disc, combo = row.adapter](int index) {
                const std::string adapter = combo->itemData(index).toString().toStdString();
                if (adapter != disc.adapter) {
                    hard_disc_draft_->set_adapter(disc, adapter);
                    disc_edited();
                }
            });
        }
        connect(row.remove, &QToolButton::clicked, this, [this, disc] {
            hard_disc_draft_->remove(disc);
            disc_edited();
        });
        connect(row.field, &QLineEdit::editingFinished, this, [this, disc, field = row.field] {
            const std::string image = field->text().trimmed().toStdString();
            if (image != disc.image) {
                hard_disc_draft_->set_image(disc, image);
                disc_edited();
            }
        });
        connect(row.clear, &QPushButton::clicked, this, [this, disc] {
            if (!disc.image.empty()) {
                hard_disc_draft_->set_image(disc, {});
                disc_edited();
            }
        });
        connect(row.browse, &QPushButton::clicked, this, [this, disc] {
            FormBrowse browse;
            browse.chooser = FormBrowse::Chooser::open_file;
            browse.label = tr("Select Hard Disc Image").toStdString();
            browse.current = disc.image;
            if (const auto path = dialog_.choose(browse)) {
                hard_disc_draft_->set_image(disc, path->toStdString());
                disc_edited();
            }
        });
        discs_.push_back(row);
    }
    set_diagnostics(diagnostics_);
}

void StorageSection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    for (DiscRow& row : discs_) {
        row.diagnostic->show_for(diagnostics, "/extensions/" + std::to_string(row.disc.index));
    }
    controller_diagnostic_->show_for(diagnostics, "/storage/fdc_socket");
    for (std::size_t i = 0; i < drives_.size(); ++i) {
        drives_[i].diagnostic->show_for(diagnostics, "/storage/floppy_drives/" + std::to_string(i));
    }
}

}  // namespace beebium::linux_client

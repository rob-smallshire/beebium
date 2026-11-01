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

#include "app/NewMachineDialog.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

constexpr int INDEX_ROLE = Qt::UserRole;
const QSize THUMBNAIL_SIZE(96, 72);

}  // namespace

std::vector<PresetListing> launchable_presets(
    const std::vector<PresetListing>& presets,
    const std::function<bool(const QString& model)>& has_server) {
    std::vector<PresetListing> launchable;
    for (const bool built_in : {true, false}) {
        for (const auto& preset : presets) {
            if (preset.built_in() == built_in && has_server(preset.model)) {
                launchable.push_back(preset);
            }
        }
    }
    return launchable;
}

NewMachineDialog::NewMachineDialog(ServerLocation servers, QString thumbnail_cache_dirpath,
                                   QWidget* parent)
    : QDialog(parent), servers_(std::move(servers)),
      thumbnail_cache_dirpath_(std::move(thumbnail_cache_dirpath)) {
    setWindowTitle(tr("New Machine"));
    auto* layout = new QVBoxLayout(this);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("presets"));
    list_->setHeaderLabels({tr("Machine"), tr("Description")});
    list_->setRootIsDecorated(false);
    list_->setIconSize(THUMBNAIL_SIZE);
    list_->header()->setStretchLastSection(true);
    layout->addWidget(list_, 1);
    status_ = new QLabel(tr("Reading the presets..."), this);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    launch_ = buttons->addButton(tr("&Launch"), QDialogButtonBox::AcceptRole);
    launch_->setDefault(true);
    launch_->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] {
        launch_->setEnabled(chosen().has_value());
    });
    connect(list_, &QTreeWidget::itemActivated, this, [this] {
        if (chosen()) {
            accept();
        }
    });

    resize(760, 560);
    load();
}

void NewMachineDialog::load() {
    // Any server lists every model's presets.
    cli_.run(servers_.executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
              QStringLiteral("--all-models")},
             [this](const CliResult& result) {
                 if (!result.ok()) {
                     status_->setText(tr("The presets could not be read.") +
                                      (result.standard_error.isEmpty()
                                           ? QString()
                                           : QStringLiteral("\n") + result.standard_error));
                     return;
                 }
                 show_presets(launchable_presets(
                     parse_preset_listing(result.standard_output), [this](const QString& model) {
                         const QFileInfo server(servers_.executable_filepath(model));
                         return server.isFile() && server.isExecutable();
                     }));
             });
}

void NewMachineDialog::show_presets(const std::vector<PresetListing>& presets) {
    presets_ = presets;
    list_->clear();
    std::optional<bool> heading_for;
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        const PresetListing& preset = presets_[i];
        if (heading_for != preset.built_in()) {
            auto* heading = new QTreeWidgetItem(
                list_, {preset.built_in() ? tr("Built In") : tr("Your Presets")});
            heading->setFlags(Qt::ItemIsEnabled);
            QFont font = heading->font(0);
            font.setBold(true);
            heading->setFont(0, font);
            heading_for = preset.built_in();
        }
        auto* item = new QTreeWidgetItem(list_, {preset.name, preset.description});
        item->setData(0, INDEX_ROLE, static_cast<int>(i));
        item->setIcon(0, style()->standardIcon(QStyle::SP_ComputerIcon));
        if (preset.has_thumbnail) {
            fetch_thumbnail(item, preset);
        }
    }
    list_->resizeColumnToContents(0);
    status_->setText(presets_.empty() ? tr("No presets can be launched with the servers found.")
                                      : QString());
}

void NewMachineDialog::fetch_thumbnail(QTreeWidgetItem* item, const PresetListing& preset) {
    QDir().mkpath(thumbnail_cache_dirpath_);
    const QString filepath = QDir(thumbnail_cache_dirpath_).filePath(preset.id + QStringLiteral(".png"));
    const auto show = [item, filepath] {
        QPixmap pixmap(filepath);
        if (!pixmap.isNull()) {
            item->setIcon(0, QIcon(pixmap.scaled(THUMBNAIL_SIZE, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation)));
        }
    };
    if (QFileInfo::exists(filepath) &&
        QFileInfo(filepath).lastModified() >= QFileInfo(preset.filepath).lastModified()) {
        show();
        return;
    }
    cli_.run(servers_.executable_filepath(preset.model),
             {QStringLiteral("show-preset"), preset.filepath, QStringLiteral("--thumbnail"),
              QStringLiteral("--output"), filepath},
             [show](const CliResult& result) {
                 if (result.ok()) {
                     show();
                 }
             });
}

std::optional<PresetListing> NewMachineDialog::chosen() const {
    const QTreeWidgetItem* item = list_->currentItem();
    if (!item || !item->data(0, INDEX_ROLE).isValid()) {
        return std::nullopt;
    }
    const int index = item->data(0, INDEX_ROLE).toInt();
    if (index < 0 || static_cast<std::size_t>(index) >= presets_.size()) {
        return std::nullopt;
    }
    return presets_[static_cast<std::size_t>(index)];
}

}  // namespace beebium::linux_client

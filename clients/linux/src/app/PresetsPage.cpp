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

#include "app/PresetsPage.hpp"

#include "app/CardGrid.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

constexpr int INDEX_ROLE = Qt::UserRole;

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

PresetsPage::PresetsPage(QString thumbnail_cache_dirpath, QWidget* parent)
    : QWidget(parent), thumbnail_cache_dirpath_(std::move(thumbnail_cache_dirpath)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    grid_ = new CardGrid(this);
    grid_->setObjectName(QStringLiteral("presets"));
    layout->addWidget(grid_, 1);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    detail_ = new QLabel(this);
    detail_->setWordWrap(true);
    detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail_->setEnabled(false);  // the secondary text colour
    detail_->hide();
    layout->addWidget(detail_);

    auto* buttons = new QHBoxLayout;
    configure_ = new QPushButton(tr("&Configure Beebium..."), this);
    configure_->hide();
    buttons->addWidget(configure_);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(configure_, &QPushButton::clicked, this, &PresetsPage::configure_requested);
    // Activating a card launches it: a click or a double-click, as the
    // desktop is set, or Return.
    connect(grid_, &QListWidget::itemActivated, this, &PresetsPage::launch_chosen);
}

void PresetsPage::set_servers(std::optional<ServerLocation> servers) {
    servers_ = std::move(servers);
    load();
}

void PresetsPage::set_launching(bool launching) {
    launching_ = launching;
}

QString PresetsPage::status_text() const {
    return status_->text();
}

QString PresetsPage::detail_text() const {
    return detail_->isHidden() ? QString() : detail_->text();
}

bool PresetsPage::offering_configure() const {
    return !configure_->isHidden();
}

void PresetsPage::launch_chosen() {
    if (launching_) {
        return;
    }
    if (const auto preset = chosen()) {
        Q_EMIT launch_requested(*preset);
    }
}

void PresetsPage::load() {
    const int generation = ++generation_;
    presets_.clear();
    grid_->clear();
    detail_->hide();
    if (!servers_) {
        status_->setText(tr("Beebium could not find its servers on this computer. Install the "
                            "beebium-server package, or say where the servers are."));
        configure_->show();
        return;
    }
    configure_->hide();
    status_->setText(tr("Reading the presets..."));
    // Any server lists every model's presets.
    QPointer<PresetsPage> guard(this);
    cli_.run(servers_->executable_filepath(QStringLiteral("model-b")),
             {QStringLiteral("--format"), QStringLiteral("jsonl"), QStringLiteral("list-presets"),
              QStringLiteral("--all-models")},
             [this, guard, generation](const CliResult& result) {
                 if (!guard || generation != generation_) {
                     return;  // superseded by a later load
                 }
                 if (!result.ok()) {
                     // Which servers, how they were chosen, and their own
                     // words: a stale or foreign set found first on PATH is
                     // then plain to see, and the way to choose others is
                     // at hand.
                     status_->setText(tr("Beebium could not read the presets from %1.")
                                          .arg(describe_servers(*servers_)));
                     detail_->setText(result.standard_error);
                     detail_->setVisible(!result.standard_error.isEmpty());
                     configure_->show();
                     return;
                 }
                 show_presets(launchable_presets(
                     parse_preset_listing(result.standard_output), [this](const QString& model) {
                         const QFileInfo server(servers_->executable_filepath(model));
                         return server.isFile() && server.isExecutable();
                     }));
             });
}

void PresetsPage::show_presets(const std::vector<PresetListing>& presets) {
    presets_ = presets;
    grid_->clear();
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        const PresetListing& preset = presets_[i];
        QListWidgetItem* item = grid_->add_card(preset.name, preset.description);
        item->setData(INDEX_ROLE, static_cast<int>(i));
        if (preset.has_thumbnail && !thumbnail_failed_.contains(preset.id)) {
            fetch_thumbnail(item, preset);
        }
    }
    if (!presets_.empty()) {
        grid_->setCurrentRow(0);
    }
    status_->setText(presets_.empty() ? tr("No presets can be launched with the servers found.")
                                      : QString());
}

void PresetsPage::fetch_thumbnail(QListWidgetItem* item, const PresetListing& preset) {
    QDir().mkpath(thumbnail_cache_dirpath_);
    const QString filepath =
        QDir(thumbnail_cache_dirpath_).filePath(preset.id + QStringLiteral(".png"));
    const int generation = generation_;
    QPointer<PresetsPage> guard(this);
    const auto show = [this, guard, generation, item, filepath] {
        // The card is gone if the catalogue was read again meanwhile.
        if (!guard || generation != generation_) {
            return;
        }
        const QPixmap pixmap(filepath);
        if (!pixmap.isNull()) {
            grid_->set_picture(item, pixmap);
        }
    };
    if (QFileInfo::exists(filepath) &&
        QFileInfo(filepath).lastModified() >= QFileInfo(preset.filepath).lastModified()) {
        show();
        return;
    }
    cli_.run(servers_->executable_filepath(preset.model),
             {QStringLiteral("show-preset"), preset.filepath, QStringLiteral("--thumbnail"),
              QStringLiteral("--output"), filepath},
             [this, guard, show, id = preset.id](const CliResult& result) {
                 if (result.ok()) {
                     show();
                 } else if (guard) {
                     thumbnail_failed_.insert(id);
                 }
             });
}

std::optional<PresetListing> PresetsPage::chosen() const {
    const QListWidgetItem* item = grid_->currentItem();
    if (!item || !item->data(INDEX_ROLE).isValid()) {
        return std::nullopt;
    }
    const int index = item->data(INDEX_ROLE).toInt();
    if (index < 0 || static_cast<std::size_t>(index) >= presets_.size()) {
        return std::nullopt;
    }
    return presets_[static_cast<std::size_t>(index)];
}

}  // namespace beebium::linux_client

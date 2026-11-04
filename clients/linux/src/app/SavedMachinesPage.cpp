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

#include "app/SavedMachinesPage.hpp"

#include "app/CardGrid.hpp"
#include "app/FileReveal.hpp"
#include "app/SavedMachines.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QStackedLayout>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

constexpr int DIRPATH_ROLE = Qt::UserRole;

}  // namespace

QString last_used_text(const MachineDescription& machine) {
    const QDateTime when = QDateTime::fromString(machine.last_used, Qt::ISODate);
    if (!when.isValid()) {
        return QObject::tr("Not run yet");
    }
    return QObject::tr("Last used %1")
        .arg(QLocale().toString(when.toLocalTime(), QLocale::ShortFormat));
}

SavedMachinesPage::SavedMachinesPage(SavedMachines& machines, QWidget* parent)
    : QWidget(parent), machines_(machines) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* area = new QWidget(this);
    stack_ = new QStackedLayout(area);
    grid_ = new CardGrid(area);
    grid_->setObjectName(QStringLiteral("savedMachines"));
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    stack_->addWidget(grid_);
    empty_ = new QWidget(area);
    empty_->setObjectName(QStringLiteral("noSavedMachines"));
    auto* empty_layout = new QVBoxLayout(empty_);
    auto* empty_title = new QLabel(tr("No recent machines yet"), empty_);
    auto* empty_hint = new QLabel(tr("Save a running machine and it appears here."), empty_);
    for (QLabel* label : {empty_title, empty_hint}) {
        label->setAlignment(Qt::AlignCenter);
        label->setEnabled(false);  // the secondary text colour
    }
    empty_layout->addStretch(1);
    empty_layout->addWidget(empty_title);
    empty_layout->addWidget(empty_hint);
    empty_layout->addStretch(1);
    stack_->addWidget(empty_);
    layout->addWidget(area, 1);

    auto* buttons = new QHBoxLayout;
    // A card's own actions are on its context menu.
    clear_ = new QPushButton(tr("C&lear List"), this);
    auto* browse = new QPushButton(tr("Open &Other..."), this);
    buttons->addWidget(clear_);
    buttons->addStretch(1);
    buttons->addWidget(browse);
    layout->addLayout(buttons);

    connect(clear_, &QPushButton::clicked, &machines_, &SavedMachines::clear);
    connect(browse, &QPushButton::clicked, this, &SavedMachinesPage::browse_requested);
    // Activating a card opens it: a click or a double-click, as the
    // desktop is set, or Return.
    connect(grid_, &QListWidget::itemActivated, this, &SavedMachinesPage::open_chosen);
    connect(grid_, &QWidget::customContextMenuRequested, this,
            &SavedMachinesPage::show_context_menu);
    connect(&machines_, &SavedMachines::changed, this, &SavedMachinesPage::rebuild);

    // The running marks are the machines' locks, so only as fresh as the
    // last description: a backstop for machines started from the command
    // line or another client.
    refresh_.setInterval(REFRESH_MS);
    connect(&refresh_, &QTimer::timeout, &machines_, &SavedMachines::reload);
    rebuild();
}

bool SavedMachinesPage::showing_empty_state() const {
    return stack_->currentWidget() == empty_;
}

void SavedMachinesPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    machines_.reload();
    refresh_.start();
}

void SavedMachinesPage::hideEvent(QHideEvent* event) {
    refresh_.stop();
    QWidget::hideEvent(event);
}

void SavedMachinesPage::rebuild() {
    // Keep the chosen card chosen across a refresh.
    const QString was_chosen = chosen();
    grid_->clear();
    for (const auto& machine : machines_.machines()) {
        const QPixmap thumbnail = machine.thumbnail_filepath.isEmpty() ||
                                          !QFileInfo::exists(machine.thumbnail_filepath)
                                      ? QPixmap()
                                      : QPixmap(machine.thumbnail_filepath);
        QListWidgetItem* item =
            grid_->add_card(machine.name, last_used_text(machine), thumbnail, machine.running());
        item->setData(DIRPATH_ROLE, machine.machine_dirpath);
        item->setToolTip(machine.name + QLatin1Char('\n') + machine.machine_dirpath);
        if (machine.machine_dirpath == was_chosen) {
            grid_->setCurrentItem(item);
        }
    }
    if (!grid_->currentItem() && grid_->count() > 0) {
        grid_->setCurrentRow(0);
    }
    stack_->setCurrentWidget(machines_.machines().empty() ? empty_ : grid_);
    update_buttons();
}

void SavedMachinesPage::update_buttons() {
    clear_->setEnabled(!machines_.machines().empty());
}

QString SavedMachinesPage::chosen() const {
    const QListWidgetItem* item = grid_->currentItem();
    return item ? item->data(DIRPATH_ROLE).toString() : QString();
}

void SavedMachinesPage::open_chosen() {
    const QString dirpath = chosen();
    if (!dirpath.isEmpty()) {
        Q_EMIT open_requested(dirpath);
    }
}

void SavedMachinesPage::show_context_menu(const QPoint& position) {
    QListWidgetItem* item = grid_->itemAt(position);
    if (!item) {
        return;
    }
    grid_->setCurrentItem(item);
    const QString dirpath = item->data(DIRPATH_ROLE).toString();
    QMenu menu(this);
    menu.addAction(tr("&Open"), this, [this, dirpath] { Q_EMIT open_requested(dirpath); });
    menu.addAction(tr("Open &Containing Folder"), this,
                   [dirpath] { reveal_file(dirpath); });
    menu.addSeparator();
    // Only a list entry: the machine on disk is untouched, so no question.
    menu.addAction(tr("&Remove from List"), this, [this, dirpath] { machines_.forget(dirpath); });
    menu.exec(grid_->viewport()->mapToGlobal(position));
}

}  // namespace beebium::linux_client

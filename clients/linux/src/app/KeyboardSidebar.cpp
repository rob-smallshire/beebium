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

#include "app/KeyboardSidebar.hpp"

#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QListWidget>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

constexpr int CHOICE_ROLE = Qt::UserRole;

}  // namespace

KeyboardSidebar::KeyboardSidebar(QString user_dirpath, QWidget* parent)
    : QWidget(parent), user_dirpath_(std::move(user_dirpath)) {
    setObjectName(QStringLiteral("keyboardSidebar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 8);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("mappings"));
    list_->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    layout->addWidget(list_, 2);

    reference_title_ = new QLabel(tr("Labelled keys"), this);
    QFont bold = reference_title_->font();
    bold.setBold(true);
    reference_title_->setFont(bold);
    layout->addWidget(reference_title_);
    reference_ = new QTreeWidget(this);
    reference_->setObjectName(QStringLiteral("mappingReference"));
    reference_->setHeaderLabels({tr("Key"), tr("Function")});
    reference_->setRootIsDecorated(false);
    reference_->setFocusPolicy(Qt::NoFocus);
    reference_->setSelectionMode(QAbstractItemView::NoSelection);
    reference_->header()->setStretchLastSection(true);
    layout->addWidget(reference_, 3);
    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("mappingReferenceHint"));
    hint_->setWordWrap(true);
    hint_->setForegroundRole(QPalette::PlaceholderText);
    hint_->hide();
    layout->addWidget(hint_);

    caps_ = new QCheckBox(tr("Synchronise Caps Lock"), this);
    caps_->setToolTip(tr("Keep the machine's Caps Lock the same as this computer's"));
    caps_->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(caps_);

    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QVariant index = item->data(CHOICE_ROLE);
        if (!index.isValid()) {
            return;  // a heading
        }
        const MappingChoice& choice = choices_[static_cast<std::size_t>(index.toInt())];
        if (!choice.usable() || choice.id == active_id_) {
            return;
        }
        active_id_ = choice.id;
        rebuild();
        Q_EMIT mapping_chosen(choice);
    });
    connect(caps_, &QCheckBox::toggled, this, &KeyboardSidebar::caps_lock_sync_toggled);
    rescan();
}

void KeyboardSidebar::set_active(const QString& id) {
    active_id_ = id;
    rebuild();
}

void KeyboardSidebar::set_caps_lock_sync(bool on) {
    const QSignalBlocker quiet(caps_);
    caps_->setChecked(on);
}

void KeyboardSidebar::rescan() {
    choices_ = mapping_choices(user_dirpath_);
    rebuild();
}

void KeyboardSidebar::showEvent(QShowEvent* event) {
    // Files may have been added or changed while it was hidden.
    rescan();
    QWidget::showEvent(event);
}

void KeyboardSidebar::rebuild() {
    list_->clear();
    std::optional<bool> heading_for;
    for (std::size_t i = 0; i < choices_.size(); ++i) {
        const MappingChoice& choice = choices_[i];
        if (heading_for != choice.built_in) {
            auto* heading = new QListWidgetItem(choice.built_in ? tr("Beebium") : tr("Custom"), list_);
            heading->setFlags(Qt::NoItemFlags);
            QFont font = heading->font();
            font.setBold(true);
            heading->setFont(font);
            heading_for = choice.built_in;
        }
        auto* item = new QListWidgetItem(choice.name, list_);
        item->setData(CHOICE_ROLE, static_cast<int>(i));
        if (!choice.usable()) {
            item->setFlags(Qt::NoItemFlags);
            item->setToolTip(tr("This mapping cannot be used: %1").arg(choice.problem));
            item->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        } else {
            item->setToolTip(choice.built_in ? choice.name : choice.filepath);
            item->setIcon(choice.id == active_id_
                              ? QIcon::fromTheme(QStringLiteral("object-select"),
                                                 style()->standardIcon(QStyle::SP_DialogApplyButton))
                              : QIcon());
        }
    }
    show_reference();
}

void KeyboardSidebar::show_reference() {
    reference_->clear();
    std::vector<ReferenceRow> rows;
    for (const MappingChoice& choice : choices_) {
        if (choice.id == active_id_ && choice.mapping) {
            rows = mapping_reference(*choice.mapping);
        }
    }
    QString hint;
    for (const ReferenceRow& row : rows) {
        auto* item = new QTreeWidgetItem(reference_, {row.host_key, row.action});
        if (!row.hint.isEmpty()) {
            item->setToolTip(0, row.hint);
            item->setToolTip(1, row.hint);
            hint = row.hint;
        }
    }
    hint_->setText(hint);
    hint_->setVisible(!hint.isEmpty());
    reference_->resizeColumnToContents(0);
    // Only a mapping that labels its keys has a table to show.
    reference_title_->setVisible(!rows.empty());
    reference_->setVisible(!rows.empty());
}

}  // namespace beebium::linux_client

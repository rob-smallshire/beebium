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
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QListWidget>
#include <QSignalBlocker>
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
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    // The selected row is the active mapping, so it shows whether or not the
    // window has the focus: some palettes draw an inactive selection in the
    // base colour.
    QPalette marked = list_->palette();
    for (const auto role : {QPalette::Highlight, QPalette::HighlightedText}) {
        marked.setColor(QPalette::Inactive, role, marked.color(QPalette::Active, role));
    }
    list_->setPalette(marked);
    layout->addWidget(list_);
    // The free space sits between the mappings and the labelled keys, which
    // keep to the foot of the mode, as on macOS.
    layout->addStretch(1);

    // The active mapping's labelled keys: a divider, then bare rows of key
    // and function, as tall as they are.
    divider_ = new QFrame(this);
    divider_->setObjectName(QStringLiteral("referenceDivider"));
    divider_->setFrameShape(QFrame::HLine);
    divider_->setFrameShadow(QFrame::Sunken);
    layout->addWidget(divider_);
    reference_ = new QTreeWidget(this);
    reference_->setObjectName(QStringLiteral("mappingReference"));
    reference_->setColumnCount(2);
    reference_->setHeaderHidden(true);
    reference_->setRootIsDecorated(false);
    reference_->setFrameShape(QFrame::NoFrame);
    reference_->viewport()->setAutoFillBackground(false);
    reference_->setFocusPolicy(Qt::NoFocus);
    reference_->setSelectionMode(QAbstractItemView::NoSelection);
    reference_->header()->setStretchLastSection(true);
    layout->addWidget(reference_);
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
            return;  // the divider
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
    // A plain list: the built-ins, then the user's mappings after a divider
    // with no text, the active mapping its selected row.
    list_->clear();
    bool divided = false;
    for (std::size_t i = 0; i < choices_.size(); ++i) {
        const MappingChoice& choice = choices_[i];
        if (!choice.built_in && !divided) {
            auto* divider = new QListWidgetItem(list_);
            divider->setFlags(Qt::NoItemFlags);
            divider->setSizeHint(QSize(0, 9));
            auto* line = new QFrame(list_);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            list_->setItemWidget(divider, line);
            divided = true;
        }
        auto* item = new QListWidgetItem(choice.name, list_);
        item->setData(CHOICE_ROLE, static_cast<int>(i));
        if (!choice.usable()) {
            item->setFlags(Qt::NoItemFlags);
            item->setToolTip(tr("This mapping cannot be used: %1").arg(choice.problem));
        } else {
            item->setToolTip(choice.built_in ? choice.name : choice.filepath);
            item->setSelected(choice.id == active_id_);
        }
    }
    show_reference();
    fit_to_rows(list_);
}

void KeyboardSidebar::fit_to_rows(QAbstractItemView* view) {
    // No taller than its rows, so the free space is the mode's, not the view's.
    int height = 2 * view->frameWidth();
    const int rows = view->model()->rowCount();
    for (int row = 0; row < rows; ++row) {
        height += view->sizeHintForRow(row);
    }
    if (auto* list = qobject_cast<QListWidget*>(view)) {
        height += 2 * list->spacing() * rows;
    }
    view->setMaximumHeight(height);
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
    fit_to_rows(reference_);
    // Only a mapping that labels its keys has a table to show.
    divider_->setVisible(!rows.empty());
    reference_->setVisible(!rows.empty());
}

}  // namespace beebium::linux_client

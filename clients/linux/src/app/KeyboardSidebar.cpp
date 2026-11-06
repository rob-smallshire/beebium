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
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QPalette>
#include <QListWidget>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

namespace {

constexpr int CHOICE_ROLE = Qt::UserRole;
constexpr int DIVIDER_HEIGHT = 9;
// The rows of mappings kept when space is short, the keys' list then giving way.
constexpr int LEAST_MAPPING_ROWS = 2;

}  // namespace

KeyboardSidebar::KeyboardSidebar(QString user_dirpath, QWidget* parent)
    : QWidget(parent), user_dirpath_(std::move(user_dirpath)) {
    setObjectName(QStringLiteral("keyboardSidebar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 8);

    // The mappings at the top and the labelled keys at the foot share one
    // area, the free space between them; placed by apportion().
    views_ = new QWidget(this);
    views_->setObjectName(QStringLiteral("mappingViews"));
    views_->installEventFilter(this);
    layout->addWidget(views_, 1);

    list_ = new QListWidget(views_);
    list_->setObjectName(QStringLiteral("mappings"));
    list_->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    // A plain list on the mode's background, as tall as its rows, as the
    // labelled keys are: no frame, and a scroll bar only when space is short.
    list_->setFrameShape(QFrame::NoFrame);
    list_->viewport()->setAutoFillBackground(false);
    list_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // The selected row is the active mapping, so it shows whether or not the
    // window has the focus: some palettes draw an inactive selection in the
    // base colour.
    QPalette marked = list_->palette();
    for (const auto role : {QPalette::Highlight, QPalette::HighlightedText}) {
        marked.setColor(QPalette::Inactive, role, marked.color(QPalette::Active, role));
    }
    list_->setPalette(marked);

    // The active mapping's labelled keys: a divider, then bare rows of key
    // and function, as tall as they are, at the foot of the area as on macOS.
    divider_ = new QFrame(views_);
    divider_->setObjectName(QStringLiteral("referenceDivider"));
    divider_->setFrameShape(QFrame::HLine);
    divider_->setFrameShadow(QFrame::Sunken);
    reference_ = new QTreeWidget(views_);
    reference_->setObjectName(QStringLiteral("mappingReference"));
    reference_->setColumnCount(2);
    reference_->setHeaderHidden(true);
    reference_->setRootIsDecorated(false);
    reference_->setFrameShape(QFrame::NoFrame);
    reference_->viewport()->setAutoFillBackground(false);
    reference_->setFocusPolicy(Qt::NoFocus);
    reference_->setSelectionMode(QAbstractItemView::NoSelection);
    reference_->header()->setStretchLastSection(true);
    reference_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // The keys the mapping lets the user switch off, one box each.
    switches_ = new QWidget(this);
    switches_->setObjectName(QStringLiteral("keySwitches"));
    switches_layout_ = new QVBoxLayout(switches_);
    switches_layout_->setContentsMargins(0, 0, 0, 0);
    auto* rule = new QFrame(switches_);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    switches_layout_->addWidget(rule);
    switches_->hide();
    layout->addWidget(switches_);

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

void KeyboardSidebar::set_key_switches(const std::vector<std::pair<std::string, bool>>& keys) {
    for (auto& [name, box] : key_boxes_) {
        box->hide();
        box->deleteLater();
    }
    key_boxes_.clear();
    for (const auto& [name, off] : keys) {
        const QString key = QString::fromStdString(name);
        auto* box = new QCheckBox(tr("Disable %1").arg(key), switches_);
        box->setObjectName(QStringLiteral("disable%1").arg(key));
        box->setFocusPolicy(Qt::NoFocus);
        box->setChecked(off);
        connect(box, &QCheckBox::toggled, this,
                [this, key](bool checked) { Q_EMIT key_switch_toggled(key, checked); });
        switches_layout_->addWidget(box);
        key_boxes_.emplace_back(key, box);
    }
    switches_->setVisible(!keys.empty());
}

QCheckBox* KeyboardSidebar::key_switch(const QString& key) const {
    for (const auto& [name, box] : key_boxes_) {
        if (name == key) {
            return box;
        }
    }
    return nullptr;
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
            divider->setSizeHint(QSize(0, DIVIDER_HEIGHT));
            auto* line = new QFrame(list_);
            // Its own height counts towards the row's until the list lays it
            // out, so it is the row's from the start.
            line->setFixedHeight(DIVIDER_HEIGHT);
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
    apportion();
}

int KeyboardSidebar::rows_height(const QAbstractItemView* view, int rows) {
    rows = std::min(rows, view->model()->rowCount());
    int height = 2 * view->frameWidth();
    for (int row = 0; row < rows; ++row) {
        height += view->sizeHintForRow(row);
    }
    if (auto* list = qobject_cast<const QListWidget*>(view)) {
        height += 2 * list->spacing() * rows;
    }
    return height;
}

int KeyboardSidebar::rows_height(const QAbstractItemView* view) {
    return rows_height(view, view->model()->rowCount());
}

bool KeyboardSidebar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == views_ && event->type() == QEvent::Resize) {
        apportion();
    }
    return QWidget::eventFilter(watched, event);
}

void KeyboardSidebar::apportion() {
    // Each list as tall as its rows, the free space between them. When the
    // area is too short for both, the mappings give way first, down to a
    // couple of rows, and only then the labelled keys: consulting the keys
    // is the frequent case, choosing a mapping the rare one.
    const int area_width = views_->width();
    const int area_height = views_->height();
    const bool labelled = reference_->topLevelItemCount() > 0;
    const int keys_wanted = labelled ? rows_height(reference_) : 0;
    const int divider_height = labelled ? divider_->sizeHint().height() : 0;
    const int mappings_wanted = rows_height(list_);
    const int mappings_least =
        std::min({mappings_wanted, rows_height(list_, LEAST_MAPPING_ROWS), area_height});
    const int mappings_height =
        std::clamp(area_height - divider_height - keys_wanted, mappings_least, mappings_wanted);
    list_->setGeometry(0, 0, area_width, mappings_height);
    if (labelled) {
        const int keys_height =
            std::clamp(area_height - mappings_height - divider_height, 0, keys_wanted);
        reference_->setGeometry(0, area_height - keys_height, area_width, keys_height);
        divider_->setGeometry(0, area_height - keys_height - divider_height, area_width,
                              divider_height);
    }
}

void KeyboardSidebar::show_reference() {
    reference_->clear();
    std::vector<ReferenceRow> rows;
    for (const MappingChoice& choice : choices_) {
        if (choice.id == active_id_ && choice.mapping) {
            rows = mapping_reference(*choice.mapping);
        }
    }
    for (const ReferenceRow& row : rows) {
        new QTreeWidgetItem(reference_, {row.host_key, row.action});
    }
    reference_->resizeColumnToContents(0);
    // Only a mapping that labels its keys has a table to show.
    divider_->setVisible(!rows.empty());
    reference_->setVisible(!rows.empty());
}

}  // namespace beebium::linux_client

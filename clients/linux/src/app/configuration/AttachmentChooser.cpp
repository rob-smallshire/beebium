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

#include "app/configuration/AttachmentChooser.hpp"

#include "app/configuration/SectionWidgets.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

QString joined(const std::vector<std::string>& values) {
    QStringList out;
    for (const auto& value : values) {
        out << QString::fromStdString(value);
    }
    return out.join(QStringLiteral(", "));
}

}  // namespace

ChooserEntry chooser_entry(const SchemaExtension& extension, const PresetSchema& schema) {
    ChooserEntry entry;
    entry.name = extension.name;
    entry.display_name = extension.display_name.empty() ? extension.name : extension.display_name;
    entry.description = extension.description;
    for (const auto& parameter : extension.parameters) {
        entry.needs.push_back(parameter.label.empty() ? parameter.key : parameter.label);
    }
    for (const auto& point : schema.attachment_points()) {
        for (const auto& provided : extension.provides) {
            if (point.id == provided) {
                entry.provides.push_back(point.display_name.empty() ? point.id : point.display_name);
            }
        }
    }
    return entry;
}

AttachmentChooser::AttachmentChooser(AttachmentChoice choice, QWidget* parent)
    : QDialog(parent), choice_(std::move(choice)) {
    setObjectName(QStringLiteral("attachmentChooser"));
    setWindowTitle(choice_.title);
    auto* layout = new QVBoxLayout(this);
    auto* panes = new QHBoxLayout;
    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    for (const auto& entry : choice_.entries) {
        auto* item = new QListWidgetItem(
            entry.name.empty() ? QString::fromStdString(entry.display_name)
                               : QStringLiteral("%1\n%2").arg(QString::fromStdString(entry.display_name),
                                                              QString::fromStdString(entry.name)),
            list_);
        item->setData(Qt::UserRole, QString::fromStdString(entry.name));
        if (entry.name == choice_.selected && !choice_.selected.empty()) {
            list_->setCurrentItem(item);
        }
    }
    panes->addWidget(list_, 1);
    auto* detail_pane = new QWidget(this);
    auto* detail_layout = new QVBoxLayout(detail_pane);
    title_ = section_heading(QString(), detail_pane);
    title_->setWordWrap(true);
    detail_layout->addWidget(title_);
    detail_ = new QLabel(detail_pane);
    detail_->setWordWrap(true);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detail_layout->addWidget(detail_);
    detail_layout->addStretch(1);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(detail_pane);
    panes->addWidget(scroll, 1);
    layout->addLayout(panes, 1);
    auto* buttons = new QDialogButtonBox(this);
    commit_ = buttons->addButton(choice_.commit, QDialogButtonBox::AcceptRole);
    commit_->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QListWidget::currentRowChanged, this, [this] { show_detail(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    resize(480, 320);
    show_detail();
}

std::string AttachmentChooser::chosen() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString().toStdString() : std::string();
}

void AttachmentChooser::show_detail() {
    const int row = list_->currentRow();
    commit_->setEnabled(row >= 0);
    if (row < 0 || static_cast<std::size_t>(row) >= choice_.entries.size()) {
        title_->clear();
        detail_->clear();
        return;
    }
    const ChooserEntry& entry = choice_.entries[static_cast<std::size_t>(row)];
    title_->setText(QString::fromStdString(entry.display_name));
    QStringList lines;
    if (!entry.description.empty()) {
        lines << QString::fromStdString(entry.description);
    }
    if (!entry.needs.empty()) {
        lines << tr("Needs: %1").arg(joined(entry.needs));
    }
    if (!entry.provides.empty()) {
        lines << tr("Provides: %1").arg(joined(entry.provides));
    }
    detail_->setText(lines.join(QStringLiteral("\n\n")));
}

}  // namespace beebium::linux_client

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

#include "app/ConnectDialog.hpp"

#include "app/DiscoveredMachines.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

constexpr int ENDPOINT_ROLE = Qt::UserRole;
constexpr int UUID_ROLE = Qt::UserRole + 1;

}  // namespace

ConnectDialog::ConnectDialog(DiscoveredMachines& machines, const std::vector<Endpoint>& recents,
                             IsOpen is_open, QWidget* parent)
    : QDialog(parent), machines_(machines), is_open_(std::move(is_open)) {
    setWindowTitle(tr("Connect to Machine"));
    auto* layout = new QVBoxLayout(this);

    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("machines"));
    list_->setHeaderLabels({tr("Machine"), tr("Model"), tr("Address")});
    list_->setRootIsDecorated(false);
    list_->header()->setStretchLastSection(true);
    layout->addWidget(list_, 1);
    list_note_ = new QLabel(this);
    list_note_->setWordWrap(true);
    layout->addWidget(list_note_);

    auto* form = new QFormLayout;
    address_ = new QComboBox(this);
    address_->setEditable(true);
    address_->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& recent : recents) {
        address_->addItem(QString::fromStdString(recent.to_string()));
    }
    // The default, whatever the recents say (4.1).
    address_->setEditText(QStringLiteral("localhost:%1").arg(Endpoint::DEFAULT_PORT));
    form->addRow(tr("&Address:"), address_);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect_ = buttons->addButton(tr("&Connect"), QDialogButtonBox::AcceptRole);
    connect_->setDefault(true);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(address_, &QComboBox::editTextChanged, this, [this] {
        picked_uuid_.clear();
        update_connect();
    });
    connect(list_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item) { choose(item); });
    connect(list_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        choose(item);
        if (endpoint()) {
            accept();
        }
    });
    connect(&machines_, &DiscoveredMachines::changed, this, &ConnectDialog::rebuild);

    rebuild();
    update_connect();
    resize(560, 420);
}

void ConnectDialog::rebuild() {
    list_->clear();
    bool this_host_heading = false;
    bool network_heading = false;
    for (const auto& machine : machines_.machines()) {
        const bool local = machine.on_this_host;
        if ((local && !this_host_heading) || (!local && !network_heading)) {
            auto* heading = new QTreeWidgetItem(
                list_, {local ? tr("On This Computer") : tr("On the Network")});
            heading->setFlags(Qt::ItemIsEnabled);
            QFont font = heading->font(0);
            font.setBold(true);
            heading->setFont(0, font);
            (local ? this_host_heading : network_heading) = true;
        }
        QString name = machine.instance_name;
        if (is_open_ && is_open_(machine)) {
            name = tr("%1 (open)").arg(name);
        }
        const QString address = QString::fromStdString(machine.endpoint().to_string());
        auto* item = new QTreeWidgetItem(list_, {name, machine.model_name, address});
        item->setData(0, ENDPOINT_ROLE, address);
        item->setData(0, UUID_ROLE, machine.uuid);
    }
    if (!machines_.available()) {
        list_note_->setText(tr("Machines cannot be discovered on this computer; type an address."));
    } else if (machines_.machines().empty()) {
        list_note_->setText(tr("No machines found yet; type an address, or wait for one to appear."));
    } else {
        list_note_->clear();
    }
    list_->resizeColumnToContents(0);
}

void ConnectDialog::choose(QTreeWidgetItem* item) {
    const QString address = item->data(0, ENDPOINT_ROLE).toString();
    if (address.isEmpty()) {
        return;  // a heading
    }
    const QString uuid = item->data(0, UUID_ROLE).toString();
    address_->setEditText(address);
    picked_uuid_ = uuid;  // after the edit, which clears it
}

void ConnectDialog::update_connect() {
    connect_->setEnabled(endpoint().has_value());
}

std::optional<Endpoint> ConnectDialog::endpoint() const {
    return parse_endpoint(address_->currentText().toStdString());
}

QString ConnectDialog::machine_uuid() const {
    return picked_uuid_;
}

}  // namespace beebium::linux_client

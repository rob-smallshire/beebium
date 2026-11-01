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

#include "app/SettingsDialog.hpp"

#include "app/ClientSettings.hpp"
#include "servers/ServerLocator.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

QWidget* with_browse(QLineEdit* edit, QWidget* parent, const QString& caption) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(edit, 1);
    auto* browse = new QPushButton(QObject::tr("Browse..."), row);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, row, [edit, row, caption] {
        const QString dirpath = QFileDialog::getExistingDirectory(row, caption, edit->text());
        if (!dirpath.isEmpty()) {
            edit->setText(dirpath);
        }
    });
    return row;
}

}  // namespace

SettingsDialog::SettingsDialog(ClientSettings& settings, QWidget* parent)
    : QDialog(parent), settings_(settings) {
    setWindowTitle(tr("Configure Beebium"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    servers_ = new QLineEdit(settings_.servers_dirpath(), this);
    servers_->setPlaceholderText(tr("Search PATH, then /opt/beebium/bin"));
    form->addRow(tr("&Servers:"), with_browse(servers_, this, tr("Where the Beebium Servers Are")));
    servers_found_ = new QLabel(this);
    servers_found_->setWordWrap(true);
    servers_found_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), servers_found_);

    roms_ = new QLineEdit(settings_.rom_dirpath(), this);
    roms_->setPlaceholderText(tr("The servers' own"));
    form->addRow(tr("&ROMs:"), with_browse(roms_, this, tr("Where the ROMs Are")));
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(servers_, &QLineEdit::textChanged, this, &SettingsDialog::show_found);
    show_found();
    resize(620, sizeHint().height());
}

void SettingsDialog::show_found() {
    const auto found = locate_servers_on_this_host(servers_->text().trimmed());
    if (found) {
        servers_found_->setText(tr("Using the servers in %1.").arg(found->servers_dirpath));
    } else if (!servers_->text().trimmed().isEmpty()) {
        servers_found_->setText(tr("There are no Beebium servers in that directory."));
    } else {
        servers_found_->setText(tr("No Beebium servers were found on this computer."));
    }
}

void SettingsDialog::accept() {
    settings_.set_servers_dirpath(servers_->text().trimmed());
    settings_.set_rom_dirpath(roms_->text().trimmed());
    QDialog::accept();
}

}  // namespace beebium::linux_client

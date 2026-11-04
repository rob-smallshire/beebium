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

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
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
    auto* pages = new QTabWidget(this);
    layout->addWidget(pages);
    auto* paths = new QWidget(pages);
    auto* paths_layout = new QVBoxLayout(paths);
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
    paths_layout->addLayout(form);
    paths_layout->addStretch(1);
    pages->addTab(paths, tr("&Paths"));

    // The macOS client's Settings > Video: three defaults, no more.
    const VideoSettings defaults = settings_.video_defaults();
    auto* video = new QWidget(pages);
    auto* video_layout = new QVBoxLayout(video);
    auto* video_form = new QFormLayout;
    default_style_ = new QComboBox(video);
    default_style_->setObjectName(QStringLiteral("defaultStyle"));
    default_style_->addItems({tr("Standard"), tr("Debug")});
    default_style_->setCurrentIndex(defaults.style == DisplayStyle::debug ? 1 : 0);
    video_form->addRow(tr("Default &style:"), default_style_);
    default_pixels_ = new QComboBox(video);
    default_pixels_->setObjectName(QStringLiteral("defaultPixels"));
    default_pixels_->addItems({tr("Authentic"), tr("Crisp")});
    default_pixels_->setCurrentIndex(defaults.pixels == PixelShape::crisp ? 1 : 0);
    video_form->addRow(tr("Default &pixels:"), default_pixels_);
    boxing_ = new ColourChoice(QStringLiteral("defaultBoxingColour"), tr("Default Boxing Colour"),
                               VideoSettings{}.boxing, video);
    boxing_->set_colour(defaults.boxing);
    auto* boxing_row = new QHBoxLayout;
    boxing_row->addWidget(boxing_->well());
    boxing_row->addWidget(boxing_->reset());
    boxing_row->addStretch(1);
    video_form->addRow(tr("Default boxing colour:"), boxing_row);
    video_layout->addLayout(video_form);
    auto* footer = new QLabel(tr("Defaults apply to new emulator windows. Existing windows keep "
                                 "their current settings."),
                              video);
    footer->setWordWrap(true);
    video_layout->addWidget(footer);
    video_layout->addStretch(1);
    pages->addTab(video, tr("&Video"));

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
    VideoSettings defaults;
    defaults.style = default_style_->currentIndex() == 1 ? DisplayStyle::debug
                                                         : DisplayStyle::standard;
    defaults.pixels = default_pixels_->currentIndex() == 1 ? PixelShape::crisp
                                                           : PixelShape::authentic;
    defaults.boxing = boxing_->colour();
    settings_.set_video_defaults(defaults);
    QDialog::accept();
}

}  // namespace beebium::linux_client

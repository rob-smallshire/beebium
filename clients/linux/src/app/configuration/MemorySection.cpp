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

#include "app/configuration/MemorySection.hpp"

#include "app/FileReveal.hpp"
#include "app/configuration/SectionWidgets.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

namespace beebium::linux_client {

namespace {

const char* const KINDS[] = {"rom", "ram", "empty"};

QString kind_label(const std::string& kind) {
    if (kind == "rom") {
        return QObject::tr("ROM");
    }
    if (kind == "ram") {
        return QObject::tr("RAM");
    }
    return QObject::tr("Empty");
}

QString file_name(const std::string& uri) {
    return QFileInfo(path_of_uri(uri)).fileName();
}

// A local image's path; empty for none or a library name.
QString image_filepath(const SocketContent& content) {
    if (content.image_uri.rfind("file:", 0) != 0) {
        return {};
    }
    return path_of_uri(content.image_uri);
}

}  // namespace

MemorySection::MemorySection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("memorySection"));
    auto* layout = new QVBoxLayout(this);
    heading_ = section_heading(QString(), this);
    layout->addWidget(heading_);
    aliasing_ = section_caption(this);
    aliasing_->setText(tr("Each socket on this machine answers several aliased slot numbers."));
    layout->addWidget(aliasing_);
    rows_layout_ = new QVBoxLayout;
    layout->addLayout(rows_layout_);
    diagnostic_ = new DiagnosticLine(this);
    layout->addWidget(diagnostic_);
    layout->addStretch(1);
    connect(&dialog_, &NewMachineDialog::rom_described, this, [this] { describe(); });
}

void MemorySection::reload() {
    const auto& schema = dialog_.schema();
    heading_->setText(schema ? QString::fromStdString(schema->section_label("sideways_bank"))
                             : QString());
    aliasing_->setVisible(schema && schema->has_aliasing());
    sideways_ = std::make_unique<SidewaysDraft>(
        dialog_.draft(), schema ? schema->sockets() : std::vector<SchemaSocket>{});

    for (Row& row : rows_) {
        row.widget->deleteLater();
    }
    rows_.clear();
    for (std::size_t i = 0; i < sideways_->sockets().size(); ++i) {
        const SchemaSocket& socket = sideways_->sockets()[i];
        const int number = static_cast<int>(i);
        Row row;
        row.widget = new QWidget(this);
        auto* outer = new QVBoxLayout(row.widget);
        outer->setContentsMargins(0, 4, 0, 4);
        auto* line = new QHBoxLayout;
        row.label = new QLabel(QString::fromStdString(socket.label), row.widget);
        row.label->setMinimumWidth(row.label->fontMetrics().horizontalAdvance(QStringLiteral("IC100 ")));
        line->addWidget(row.label);
        auto* text = new QVBoxLayout;
        row.title = new QLabel(row.widget);
        row.caption = section_caption(row.widget);
        text->addWidget(row.title);
        text->addWidget(row.caption);
        line->addLayout(text, 1);
        row.kind = new QComboBox(row.widget);
        row.kind->setObjectName(QStringLiteral("socketKind%1").arg(number));
        for (const char* kind : KINDS) {
            if (socket.can(kind)) {
                row.kind->addItem(kind_label(kind), QString::fromLatin1(kind));
            }
        }
        line->addWidget(row.kind);
        row.padlock = new QToolButton(row.widget);
        row.padlock->setObjectName(QStringLiteral("socketPadlock%1").arg(number));
        row.padlock->setCheckable(true);
        row.padlock->setToolTip(tr("Write-Protect"));
        row.padlock->setAccessibleName(tr("Write-Protect"));
        line->addWidget(row.padlock);
        row.actions = new QToolButton(row.widget);
        row.actions->setText(QStringLiteral("..."));
        row.actions->setAccessibleName(tr("Actions"));
        line->addWidget(row.actions);
        outer->addLayout(line);
        row.diagnostic = new DiagnosticLine(row.widget);
        outer->addWidget(row.diagnostic);
        rows_layout_->addWidget(row.widget);

        connect(row.kind, &QComboBox::activated, this, [this, number](int index) {
            const std::string kind = rows_[static_cast<std::size_t>(number)]
                                         .kind->itemData(index).toString().toStdString();
            const SocketContent was = sideways_->content(static_cast<std::size_t>(number));
            if (kind == was.type) {
                return;
            }
            // A ROM is its image: without one, choose it first.
            if (kind == "rom" && was.image_uri.empty()) {
                browse(number);
                return;
            }
            // A ROM's image stays as the RAM's preload, and back.
            SocketContent content = was;
            content.type = kind;
            if (kind != "ram") {
                content.write_protected = false;
            }
            if (kind == "empty") {
                content.image_uri.clear();
            }
            set(number, content);
        });
        connect(row.padlock, &QToolButton::toggled, this, [this, number](bool engaged) {
            SocketContent content = sideways_->content(static_cast<std::size_t>(number));
            content.write_protected = engaged;
            set(number, content);
        });
        connect(row.actions, &QToolButton::clicked, this, [this, number] {
            QMenu* menu = row_menu(number);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            const Row& r = rows_[static_cast<std::size_t>(number)];
            menu->popup(r.actions->mapToGlobal(QPoint(0, r.actions->height())));
        });
        rows_.push_back(row);
    }
    describe();
    set_diagnostics(diagnostics_);
}

void MemorySection::describe() {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        Row& row = rows_[i];
        const SchemaSocket& socket = sideways_->sockets()[i];
        const SocketContent content = sideways_->content(i);
        const RomDescription* rom = dialog_.rom_description(content.image_uri);

        QString title;
        if (content.type == "empty") {
            title = tr("Empty");
        } else if (content.type == "ram") {
            title = content.image_uri.empty()
                        ? tr("Sideways RAM")
                        : tr("Sideways RAM (preload: %1)").arg(file_name(content.image_uri));
        } else if (content.image_uri.empty()) {
            title = tr("ROM (no image)");
        } else {
            title = rom ? rom->display_title() : QString();
            if (title.isEmpty()) {
                title = file_name(content.image_uri);
            }
        }
        row.label->setText(QString::fromStdString(socket.label) +
                           (sideways_->changed(i) ? QStringLiteral(" \u2022") : QString()));
        row.title->setText(title);
        row.title->setEnabled(content.type != "empty");

        QStringList slots;
        for (int slot : socket.slots) {
            slots << QString::number(slot);
        }
        QStringList caption{(socket.slots.size() > 1 ? tr("Slots %1") : tr("Slot %1"))
                                .arg(slots.join(QStringLiteral(", "))),
                            tr("priority %1").arg(socket.priority())};
        if (rom && rom->recognised) {
            caption << rom->display_kinds();
        }
        row.caption->setText(caption.join(QStringLiteral(" · ")));

        {
            const QSignalBlocker quiet(row.kind);
            row.kind->setCurrentIndex(row.kind->findData(QString::fromStdString(content.type)));
        }
        {
            const QSignalBlocker quiet(row.padlock);
            row.padlock->setChecked(content.write_protected);
            row.padlock->setIcon(QIcon::fromTheme(content.write_protected
                                                      ? QStringLiteral("object-locked")
                                                      : QStringLiteral("object-unlocked")));
            if (row.padlock->icon().isNull()) {
                row.padlock->setText(tr("Write-Protect"));
            }
        }
        row.padlock->setVisible(content.type == "ram" && socket.can("write_protect"));
    }
}

QMenu* MemorySection::row_menu(int number) {
    const std::size_t i = static_cast<std::size_t>(number);
    const SchemaSocket& socket = sideways_->sockets()[i];
    const SocketContent content = sideways_->content(i);
    auto* menu = new QMenu;
    QAction* choose = menu->addAction(tr("Browse for ROM Image..."));
    choose->setEnabled(socket.can("rom") || content.type == "ram");
    connect(choose, &QAction::triggered, this, [this, number] { browse(number); });
    QAction* clear = menu->addAction(tr("Clear"));
    clear->setEnabled(content.type != "empty" && socket.can("empty"));
    connect(clear, &QAction::triggered, this, [this, number] { set(number, SocketContent{}); });
    menu->addSeparator();
    const QString path = image_filepath(content);
    QAction* copy = menu->addAction(tr("Copy Path"));
    copy->setEnabled(!path.isEmpty());
    connect(copy, &QAction::triggered, menu, [path] { QApplication::clipboard()->setText(path); });
    QAction* reveal = menu->addAction(tr("Open Containing Folder"));
    // The draft's server runs on this host.
    const QString refusal = path.isEmpty() ? QString() : reveal_refusal(path, true);
    reveal->setEnabled(!path.isEmpty() && refusal.isEmpty());
    reveal->setToolTip(refusal);
    connect(reveal, &QAction::triggered, menu, [path] { reveal_file(path); });
    if (socket.can("write_protect") && content.type == "ram") {
        menu->addSeparator();
        QAction* protect = menu->addAction(tr("Write-Protect"));
        protect->setCheckable(true);
        protect->setChecked(content.write_protected);
        connect(protect, &QAction::triggered, this, [this, number](bool engaged) {
            SocketContent edited = sideways_->content(static_cast<std::size_t>(number));
            edited.write_protected = engaged;
            set(number, edited);
        });
    }
    menu->addSeparator();
    QAction* up = menu->addAction(tr("Move Up"));
    up->setEnabled(i > 0);
    connect(up, &QAction::triggered, this, [this, i] {
        sideways_->swap(i, i - 1);
        reload();
        Q_EMIT edited();
    });
    QAction* down = menu->addAction(tr("Move Down"));
    down->setEnabled(i + 1 < rows_.size());
    connect(down, &QAction::triggered, this, [this, i] {
        sideways_->swap(i, i + 1);
        reload();
        Q_EMIT edited();
    });
    // As resolved, where the socket has changed from it.
    if (sideways_->changed(i)) {
        menu->addSeparator();
        QAction* revert = menu->addAction(tr("Revert to Default"));
        connect(revert, &QAction::triggered, this, [this, number] {
            set(number, sideways_->resolved(static_cast<std::size_t>(number)));
        });
    }
    return menu;
}

void MemorySection::browse(int number) {
    const std::size_t i = static_cast<std::size_t>(number);
    FormBrowse request;
    request.chooser = FormBrowse::Chooser::open_file;
    request.label = sideways_->sockets()[i].label;
    request.current = image_filepath(sideways_->content(i)).toStdString();
    const auto path = dialog_.choose(request);
    if (!path || path->isEmpty()) {
        describe();  // the kind as it was
        return;
    }
    // Into RAM, the image is its preload; anywhere else, a ROM.
    SocketContent content = sideways_->content(i);
    if (content.type != "ram") {
        content = SocketContent{"rom", {}, false};
    }
    content.image_uri = uri_of_path(*path);
    set(number, content);
}

void MemorySection::set(int number, const SocketContent& content) {
    const std::size_t i = static_cast<std::size_t>(number);
    if (sideways_->content(i) == content) {
        describe();
        return;
    }
    sideways_->set(i, content);
    describe();
    set_diagnostics(diagnostics_);
    Q_EMIT edited();
}

void MemorySection::set_diagnostics(const std::vector<PresetDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    std::vector<PresetDiagnostic> unplaced;
    std::vector<std::string> placed;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto index = sideways_->entry_index(i);
        const std::string pointer =
            index ? "/sideways_bank/slots/" + std::to_string(*index) : std::string();
        if (pointer.empty()) {
            rows_[i].diagnostic->show_for({}, "/");
        } else {
            rows_[i].diagnostic->show_for(diagnostics, pointer);
            placed.push_back(pointer);
        }
    }
    for (const auto& d : diagnostics) {
        bool in_row = false;
        for (const auto& pointer : placed) {
            in_row = in_row || d.path == pointer || d.path.rfind(pointer + "/", 0) == 0;
        }
        if (!in_row) {
            unplaced.push_back(d);
        }
    }
    diagnostic_->show_for(unplaced, "/sideways_bank");
}

}  // namespace beebium::linux_client

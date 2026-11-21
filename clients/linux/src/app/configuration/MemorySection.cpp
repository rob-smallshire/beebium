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
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace beebium::linux_client {

using nlohmann::ordered_json;

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

QString file_name(const std::string& reference) {
    return QFileInfo(path_of_uri(reference)).fileName();
}

// A local image's path; empty for none or a library name. The server reads
// a sideways image as a name or a path; a file:// URI, should a draft hold
// one, is read as its path.
QString image_filepath(const SocketContent& content) {
    if (content.image_uri.rfind("file:", 0) == 0) {
        return path_of_uri(content.image_uri);
    }
    const QString path = QString::fromStdString(content.image_uri);
    return QDir::isAbsolutePath(path) ? path : QString();
}

}  // namespace

MemorySection::MemorySection(NewMachineDialog& dialog) : dialog_(dialog) {
    setObjectName(QStringLiteral("memorySection"));
    auto* layout = new QVBoxLayout(this);
    // The MOS ROM first, as the memory map has the OS over the sideways
    // banks and the running machine's Memory mode shows it (5.8, 11).
    mos_ = new QWidget(this);
    auto* mos_layout = new QVBoxLayout(mos_);
    mos_layout->setContentsMargins(0, 0, 0, 8);
    mos_heading_ = section_heading(QString(), mos_);
    mos_layout->addWidget(mos_heading_);
    auto* mos_line = new QHBoxLayout;
    auto* mos_text = new QVBoxLayout;
    mos_title_ = new QLabel(mos_);
    mos_title_->setObjectName(QStringLiteral("mosImage"));
    mos_caption_ = section_caption(mos_);
    mos_text->addWidget(mos_title_);
    mos_text->addWidget(mos_caption_);
    mos_line->addLayout(mos_text, 1);
    mos_browse_ = new QPushButton(tr("Browse..."), mos_);
    mos_browse_->setObjectName(QStringLiteral("mosBrowse"));
    mos_line->addWidget(mos_browse_);
    mos_default_ = new QPushButton(tr("Use Default"), mos_);
    mos_default_->setObjectName(QStringLiteral("mosDefault"));
    mos_line->addWidget(mos_default_);
    mos_layout->addLayout(mos_line);
    mos_diagnostic_ = new DiagnosticLine(mos_);
    mos_layout->addWidget(mos_diagnostic_);
    layout->addWidget(mos_);
    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    mos_rule_ = rule;
    layout->addWidget(rule);
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
    connect(&dialog_, &NewMachineDialog::rom_described, this, [this] {
        describe();
        show_mos();
    });
    connect(&dialog_, &NewMachineDialog::draft_rederived, this, [this] { describe(); });
    // A cartridge fitted or removed under Peripherals brings or takes its
    // banks' sockets (5.9).
    connect(&dialog_, &NewMachineDialog::draft_changed, this, [this] {
        if (fits() != fits_) {
            reload();
        }
    });
    connect(mos_browse_, &QPushButton::clicked, this, [this] { browse_mos(); });
    // Use Default returns to the preset's MOS, as resolved (5.8).
    connect(mos_default_, &QPushButton::clicked, this, [this] {
        if (!dialog_.draft().changed("/mos_image_uri")) {
            return;
        }
        dialog_.draft().revert("/mos_image_uri");
        show_mos();
        Q_EMIT edited();
    });
}

void MemorySection::reload() {
    const auto& schema = dialog_.schema();
    heading_->setText(schema ? QString::fromStdString(schema->section_label("sideways_bank"))
                             : QString());
    aliasing_->setVisible(schema && schema->has_aliasing());
    show_mos();
    const std::vector<DraftSocket> fitted =
        schema ? draft_sockets(*schema, dialog_.draft()) : std::vector<DraftSocket>{};
    std::vector<SchemaSocket> sockets;
    for (const DraftSocket& socket : fitted) {
        sockets.push_back(socket.socket);
    }
    sideways_ = std::make_unique<SidewaysDraft>(dialog_.draft(), sockets);
    fits_ = fits();
    revealed_ = -1;

    for (Row& row : rows_) {
        row.widget->deleteLater();
    }
    rows_.clear();
    for (std::size_t i = 0; i < sideways_->sockets().size(); ++i) {
        const SchemaSocket& socket = sideways_->sockets()[i];
        const int number = static_cast<int>(i);
        Row row;
        for (const DraftSocket& f : fitted) {
            if (f.socket.slots == socket.slots) {
                row.fit = f;
            }
        }
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
        row.cartridge = section_link(tr("Fit a Cartridge in Peripherals"), row.widget);
        row.cartridge->setObjectName(QStringLiteral("socketCartridge%1").arg(number));
        text->addWidget(row.cartridge, 0, Qt::AlignLeft);
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
            content.from_mos = false;
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
            content.from_mos = false;
            set(number, content);
        });
        connect(row.cartridge, &QPushButton::clicked, this, [this, point = socket.provided_by_point] {
            dialog_.reveal(SidebarMode::peripherals, point);
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
        // No socket until a cartridge brings one (5.9).
        const bool unfitted = row.fit.unfitted();

        QString title;
        if (unfitted) {
            title = tr("No cartridge");
        } else if (content.type == "empty") {
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
        row.title->setEnabled(content.type != "empty" && !unfitted);

        QStringList slots;
        for (int slot : socket.slots) {
            slots << QString::number(slot);
        }
        QStringList caption{(socket.slots.size() > 1 ? tr("Slots %1") : tr("Slot %1"))
                                .arg(slots.join(QStringLiteral(", "))),
                            tr("priority %1").arg(socket.priority())};
        if (unfitted) {
            caption << tr("a cartridge in the %1 provides this socket")
                           .arg(QString::fromStdString(row.fit.point_name));
            if (!content.image_uri.empty()) {
                caption << tr("image %1").arg(file_name(content.image_uri));
            }
        } else if (!row.fit.provider.empty()) {
            caption << tr("%1 in the %2")
                           .arg(QString::fromStdString(row.fit.provider),
                                QString::fromStdString(row.fit.point_name));
        }
        if (rom && rom->recognised && !unfitted) {
            caption << rom->display_kinds();
        }
        if (content.from_mos) {
            caption << tr("From the MOS");
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
        row.padlock->setVisible(content.type == "ram" && socket.can("write_protect") && !unfitted);
        row.kind->setVisible(!unfitted);
        row.cartridge->setVisible(unfitted);
        // No menu where there is no socket, but for an image the draft
        // names there all the same, to clear.
        row.actions->setVisible(!unfitted || content.type != "empty");
    }
}

std::vector<std::string> MemorySection::fits() const {
    std::vector<std::string> out;
    if (const auto& schema = dialog_.schema()) {
        for (const DraftSocket& socket : draft_sockets(*schema, dialog_.draft())) {
            std::string fit = socket.provider;
            for (const auto& capability : socket.socket.capabilities) {
                fit += " " + capability;
            }
            out.push_back(fit);
        }
    }
    return out;
}

int MemorySection::row_at_slot(int slot) const {
    if (!sideways_) {
        return -1;
    }
    const auto& sockets = sideways_->sockets();
    for (std::size_t i = 0; i < sockets.size(); ++i) {
        if (std::find(sockets[i].slots.begin(), sockets[i].slots.end(), slot) != sockets[i].slots.end()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MemorySection::reveal(const std::string& target) {
    bool ok = false;
    const int row = QString::fromStdString(target).toInt(&ok);
    revealed_ = ok ? row_at_slot(row) : -1;
    if (revealed_ >= 0) {
        mark_revealed(rows_[static_cast<std::size_t>(revealed_)].widget);
    }
}

std::string MemorySection::default_mos() const {
    const auto& schema = dialog_.schema();
    const ordered_json* section = schema ? schema->section("mos_image_uri") : nullptr;
    return section ? section->value("default", std::string()) : std::string();
}

void MemorySection::show_mos() {
    const auto& schema = dialog_.schema();
    const bool shown = schema && schema->section("mos_image_uri");
    mos_->setVisible(shown);
    mos_rule_->setVisible(shown);
    if (!shown) {
        return;
    }
    mos_heading_->setText(QString::fromStdString(schema->section_label("mos_image_uri")));
    const auto value = dialog_.draft().get("/mos_image_uri");
    std::string reference = value && value->is_string() ? value->get<std::string>() : std::string();
    if (reference.empty()) {
        reference = default_mos();
    }
    // The server reads the MOS as a name on the ROM search path or a path;
    // its title is the server's, from describe-rom (5.8).
    const QString name = file_name(reference);
    const QString path = QDir::isAbsolutePath(QString::fromStdString(reference))
                             ? QString::fromStdString(reference)
                             : QString();
    const RomDescription* rom = dialog_.rom_description(reference);
    const QString title = rom && rom->recognised ? rom->display_title() : QString();
    if (title.isEmpty()) {
        mos_title_->setText(name);
        mos_caption_->clear();
    } else {
        mos_title_->setText(title);
        mos_caption_->setText(name);
    }
    mos_caption_->setVisible(!title.isEmpty());
    mos_title_->setToolTip(path);
    mos_default_->setEnabled(dialog_.draft().changed("/mos_image_uri"));
    const auto& preset_mos = dialog_.draft().baseline().value("mos_image_uri", ordered_json());
    mos_default_->setToolTip(preset_mos.is_string() ? QString::fromStdString(preset_mos.get<std::string>())
                                                    : QString());
}

void MemorySection::browse_mos() {
    FormBrowse request;
    request.key = "mos-image";
    request.chooser = FormBrowse::Chooser::open_file;
    request.label = dialog_.schema() ? dialog_.schema()->section_label("mos_image_uri") : "MOS ROM";
    const auto value = dialog_.draft().get("/mos_image_uri");
    if (value && value->is_string() && QDir::isAbsolutePath(QString::fromStdString(value->get<std::string>()))) {
        request.current = value->get<std::string>();
    }
    const auto path = dialog_.choose(request);
    if (!path || path->isEmpty()) {
        return;
    }
    // A path, as a sideways image's is: the server reads either.
    set_mos(QFileInfo(*path).absoluteFilePath().toStdString());
}

void MemorySection::set_mos(const std::string& reference) {
    if (reference.empty() || dialog_.draft().get("/mos_image_uri") == ordered_json(reference)) {
        return;
    }
    dialog_.draft().set("/mos_image_uri", reference);
    show_mos();
    Q_EMIT edited();
}

QMenu* MemorySection::row_menu(int number) {
    const std::size_t i = static_cast<std::size_t>(number);
    const SchemaSocket& socket = sideways_->sockets()[i];
    const SocketContent content = sideways_->content(i);
    auto* menu = new QMenu;
    if (rows_[i].fit.unfitted()) {
        // Nothing to load until a cartridge is fitted; an image the draft
        // names for the bank all the same can be cleared.
        QAction* clear = menu->addAction(tr("Clear"));
        clear->setEnabled(content.type != "empty");
        connect(clear, &QAction::triggered, this, [this, number] { set(number, SocketContent{}); });
        if (sideways_->changed(i)) {
            QAction* revert = menu->addAction(tr("Revert to Default"));
            connect(revert, &QAction::triggered, this, [this, number] {
                set(number, sideways_->resolved(static_cast<std::size_t>(number)));
            });
        }
        return menu;
    }
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
    // Not into a socket no cartridge provides.
    up->setEnabled(i > 0 && !rows_[i - 1].fit.unfitted());
    connect(up, &QAction::triggered, this, [this, i] {
        sideways_->swap(i, i - 1);
        reload();
        Q_EMIT edited();
    });
    QAction* down = menu->addAction(tr("Move Down"));
    down->setEnabled(i + 1 < rows_.size() && !rows_[i + 1].fit.unfitted());
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
    request.key = "rom-image";
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
    content.from_mos = false;  // the user's image
    // A path, not a file:// URI as a disc's is: the server reads a ROM image
    // as a name or a path.
    content.image_uri = QFileInfo(*path).absoluteFilePath().toStdString();
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
    mos_diagnostic_->show_for(diagnostics, "/mos_image_uri");
}

}  // namespace beebium::linux_client

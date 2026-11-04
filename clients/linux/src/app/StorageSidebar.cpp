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

#include "app/StorageSidebar.hpp"

#include "app/ClientSettings.hpp"
#include "app/ErrorPresentation.hpp"
#include "app/FileManager.hpp"
#include "servers/CoreCli.hpp"
#include "servers/ServerLocator.hpp"

#include "disc.pb.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace beebium::linux_client {

QString disc_error_marker(int kind) {
    switch (kind) {
    case beebium::DISC_ERROR_KIND_CANNOT_OPEN: return QObject::tr("Cannot open");
    case beebium::DISC_ERROR_KIND_EMPTY: return QObject::tr("Empty disc image");
    case beebium::DISC_ERROR_KIND_READ_ERROR: return QObject::tr("Read error");
    case beebium::DISC_ERROR_KIND_UNRECOGNISED: return QObject::tr("Unrecognised format");
    case beebium::DISC_ERROR_KIND_LOAD_FAILED: return QObject::tr("Couldn't load disc");
    case beebium::DISC_ERROR_KIND_NO_CONTROLLER: return QObject::tr("No disc controller");
    case beebium::DISC_ERROR_KIND_INVALID_DRIVE: return QObject::tr("Invalid drive");
    case beebium::DISC_ERROR_KIND_DRIVE_OCCUPIED: return QObject::tr("Drive occupied");
    default: return {};
    }
}

QString create_disc_error_marker(int kind) {
    switch (kind) {
    case beebium::CREATE_DISC_ERROR_KIND_UNKNOWN_LABEL: return QObject::tr("Unknown template");
    case beebium::CREATE_DISC_ERROR_KIND_DESTINATION_EXISTS: return QObject::tr("File exists");
    case beebium::CREATE_DISC_ERROR_KIND_WRITE_FAILED: return QObject::tr("Couldn't write disc");
    case beebium::CREATE_DISC_ERROR_KIND_WRONG_EXTENSION: return QObject::tr("Wrong extension");
    default: return {};
    }
}

QString create_disc_error_sentence(int kind) {
    switch (kind) {
    case beebium::CREATE_DISC_ERROR_KIND_UNKNOWN_LABEL:
        return QObject::tr("That blank disc template is not available.");
    case beebium::CREATE_DISC_ERROR_KIND_DESTINATION_EXISTS:
        return QObject::tr("A file already exists there.");
    case beebium::CREATE_DISC_ERROR_KIND_WRITE_FAILED:
        return QObject::tr("The disc image could not be written.");
    case beebium::CREATE_DISC_ERROR_KIND_WRONG_EXTENSION:
        return QObject::tr("That name has the wrong file extension for this disc.");
    default:
        return QObject::tr("The disc image could not be created.");
    }
}

QStringList recent_disc_labels(const QStringList& paths) {
    QStringList labels;
    for (const QString& path : paths) {
        const QString name = QFileInfo(path).fileName();
        int same = 0;
        for (const QString& other : paths) {
            if (QFileInfo(other).fileName() == name) {
                ++same;
            }
        }
        labels << (same > 1 ? QStringLiteral("%1 (%2)").arg(name, QFileInfo(path).dir().dirName())
                            : name);
    }
    return labels;
}

namespace {

QPixmap motor_dot() {
    QPixmap pixmap(10, 10);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2e, 0xb8, 0x4d));
    painter.drawEllipse(1, 1, 8, 8);
    return pixmap;
}

}  // namespace

DriveRow::DriveRow(SessionDiscs& discs, int number, ClientSettings* settings, Shares shares,
                   Extensions extensions, QWidget* parent)
    : QWidget(parent), discs_(discs), number_(number), settings_(settings),
      shares_(std::move(shares)), extensions_(std::move(extensions)) {
    setObjectName(QStringLiteral("drive%1").arg(number));
    setAcceptDrops(true);
    setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    auto* header = new QHBoxLayout;
    title_ = new QLabel(tr("Floppy Disc Drive %1").arg(number), this);
    QFont bold = title_->font();
    bold.setBold(true);
    title_->setFont(bold);
    motor_ = new QLabel(this);
    motor_->setPixmap(motor_dot());
    motor_->setToolTip(tr("Motor running"));
    motor_->hide();
    header->addWidget(title_);
    header->addWidget(motor_);
    header->addStretch(1);
    layout->addLayout(header);

    // The one content line: the state, or instead of it a message (9.6),
    // never both, so the sidebar does not jump.
    state_ = new QLabel(this);
    state_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail_line_ = new QLabel(this);
    detail_line_->setEnabled(false);  // the secondary text colour
    layout->addWidget(state_);
    layout->addWidget(detail_line_);
    message_line_ = new QWidget(this);
    auto* message_layout = new QHBoxLayout(message_line_);
    message_layout->setContentsMargins(0, 0, 0, 0);
    auto* warning = new QLabel(message_line_);
    warning->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(14, 14));
    message_marker_ = new QLabel(message_line_);
    details_ = new QPushButton(tr("Details..."), message_line_);
    details_->setFlat(true);
    dismiss_ = new QToolButton(message_line_);
    dismiss_->setText(QStringLiteral("x"));
    dismiss_->setAutoRaise(true);
    dismiss_->setToolTip(tr("Dismiss this error"));
    for (QWidget* widget : {static_cast<QWidget*>(details_), static_cast<QWidget*>(dismiss_)}) {
        widget->setFocusPolicy(Qt::NoFocus);
    }
    message_layout->addWidget(warning);
    message_layout->addWidget(message_marker_, 1);
    message_layout->addWidget(details_);
    message_layout->addWidget(dismiss_);
    layout->addWidget(message_line_);
    message_line_->hide();

    auto* actions = new QHBoxLayout;
    // Browse and Eject as bare icons, as the macOS client's: a folder whose
    // click browses, with a small arrow beside it for the menu (Recent, New
    // Blank Disc), and an eject glyph. Flat until the pointer is over them.
    browse_ = new QToolButton(this);
    browse_->setObjectName(QStringLiteral("browse"));
    browse_->setText(tr("Browse..."));
    browse_->setToolTip(tr("Choose a disc image to insert; the arrow offers recent and blank "
                           "discs"));
    browse_->setIcon(QIcon::fromTheme(QStringLiteral("folder"),
                                      style()->standardIcon(QStyle::SP_DirIcon)));
    browse_->setPopupMode(QToolButton::MenuButtonPopup);
    menu_ = new QMenu(browse_);
    browse_->setMenu(menu_);
    eject_ = new QToolButton(this);
    eject_->setObjectName(QStringLiteral("eject"));
    eject_->setText(tr("Eject"));
    eject_->setToolTip(tr("Eject disc"));
    eject_->setIcon(QIcon::fromTheme(QStringLiteral("media-eject"),
                                     style()->standardIcon(QStyle::SP_ArrowUp)));
    for (QToolButton* button : {browse_, eject_}) {
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setIconSize(QSize(20, 20));
    }
    cancel_ = new QPushButton(tr("Cancel"), this);
    cancel_->setToolTip(tr("Stop ejecting and keep the disc in the drive"));
    force_ = new QPushButton(tr("Force Eject"), this);
    force_->setToolTip(tr("Remove the disc now, without waiting for the drive to stop"));
    for (QWidget* widget : {static_cast<QWidget*>(browse_), static_cast<QWidget*>(eject_),
                            static_cast<QWidget*>(cancel_), static_cast<QWidget*>(force_)}) {
        widget->setFocusPolicy(Qt::NoFocus);  // the keys stay the machine's
    }
    actions->addWidget(browse_);
    actions->addStretch(1);
    actions->addWidget(cancel_);
    actions->addWidget(force_);
    actions->addWidget(eject_);
    layout->addLayout(actions);

    connect(browse_, &QToolButton::clicked, this, &DriveRow::browse);
    connect(menu_, &QMenu::aboutToShow, this, &DriveRow::rebuild_menu);
    connect(eject_, &QToolButton::clicked, this, [this] { discs_.eject(number_, false); });
    connect(cancel_, &QPushButton::clicked, this, [this] { discs_.cancel_eject(number_); });
    connect(force_, &QPushButton::clicked, this, [this] { discs_.eject(number_, true); });
    connect(details_, &QPushButton::clicked, this, &DriveRow::show_details);
    connect(dismiss_, &QToolButton::clicked, this, &DriveRow::clear_message);
    notice_timer_.setSingleShot(true);
    notice_timer_.setInterval(NOTICE_MS);
    connect(&notice_timer_, &QTimer::timeout, this, [this] {
        if (message_.kind == DriveMessage::Kind::notice) {
            clear_message();
        }
    });
    stall_timer_.setSingleShot(true);
    stall_timer_.setInterval(EJECT_STALL_MS);
    connect(&stall_timer_, &QTimer::timeout, this, [this] {
        eject_stalled_ = true;
        present();
    });
    refresh();
}

void DriveRow::refresh() {
    const DriveInfo* info = drive();
    const DriveInfo::State state = info ? info->state : DriveInfo::State::empty;
    if (state != shown_state_) {
        if (state == DriveInfo::State::ejecting) {
            eject_stalled_ = false;
            stall_timer_.start();
        } else {
            stall_timer_.stop();
            eject_stalled_ = false;
        }
        // An eject ends what a failure on the old disc said.
        if (state == DriveInfo::State::empty && message_.kind == DriveMessage::Kind::failure &&
            shown_state_ != DriveInfo::State::empty) {
            message_ = {};
        }
        shown_state_ = state;
    }
    present();
}

QString DriveRow::detail_text() const {
    return detail_line_->isVisibleTo(this) ? detail_line_->text() : QString();
}

QString DriveRow::state_text() const {
    return state_->text();
}

void DriveRow::present() {
    const DriveInfo* info = drive();
    const bool local = shares_ && shares_();
    const DriveInfo::State state = info ? info->state : DriveInfo::State::empty;
    motor_->setVisible(info && info->motor_on);

    const bool messaging = message_.kind != DriveMessage::Kind::none;
    message_line_->setVisible(messaging);
    message_marker_->setText(message_.marker);
    message_marker_->setToolTip(message_.detail.isEmpty() ? message_.marker : message_.detail);
    details_->setVisible(message_.kind == DriveMessage::Kind::failure && !message_.detail.isEmpty());
    dismiss_->setVisible(message_.kind == DriveMessage::Kind::failure);
    state_->setVisible(!messaging);

    QFont font = state_->font();
    font.setWeight(state == DriveInfo::State::loaded ? QFont::Medium : QFont::Normal);
    state_->setFont(font);
    state_->setEnabled(state != DriveInfo::State::empty);
    switch (state) {
    case DriveInfo::State::empty:
        state_->setText(local ? tr("Empty - Drop disc image here")
                              : tr("Empty - server is on another host"));
        state_->setToolTip({});
        break;
    case DriveInfo::State::loaded:
    case DriveInfo::State::ejecting: {
        state_->setText(state == DriveInfo::State::ejecting
                            ? (eject_stalled_ ? tr("Ejecting - drive busy") : tr("Ejecting..."))
                            : info->disc_name);
        // The name only; the path is in the tooltip and the context menu.
        state_->setToolTip(path_for_disc_url(info->disc_url));
        // No format or sides: the server knows them, and the row reads
        // better without them.
        detail_line_->setText(state == DriveInfo::State::ejecting ? info->disc_name
                              : info->write_protected             ? tr("Read-only")
                                                                  : QString());
        break;
    }
    }

    // Shown only with something to say, so no blank band.
    detail_line_->setVisible(!messaging && state != DriveInfo::State::empty &&
                             !detail_line_->text().isEmpty());

    // Browse hands the server a path: an empty drive, a server sharing this
    // filesystem (3.2).
    browse_->setEnabled(state == DriveInfo::State::empty && local);
    browse_->setToolTip(local ? tr("Choose a disc image to insert")
                              : tr("The server is on another host, so it cannot open files here"));
    eject_->setVisible(state != DriveInfo::State::ejecting);
    eject_->setEnabled(state == DriveInfo::State::loaded);
    cancel_->setVisible(state == DriveInfo::State::ejecting);
    force_->setVisible(state == DriveInfo::State::ejecting && eject_stalled_);
}

void DriveRow::show_failure(const QString& marker, const QString& sentence,
                            const QString& detail) {
    notice_timer_.stop();
    message_ = {DriveMessage::Kind::failure, marker, sentence, detail};
    present();
}

void DriveRow::show_notice(const QString& text) {
    // A failure still standing is not hidden by a passing notice.
    if (message_.kind == DriveMessage::Kind::failure) {
        return;
    }
    message_ = {DriveMessage::Kind::notice, text, {}, {}};
    notice_timer_.start();
    present();
}

void DriveRow::clear_message() {
    notice_timer_.stop();
    message_ = {};
    present();
}

void DriveRow::show_details() {
    auto* dialog = new ServerMessageDialog(tr("Disc Problem"), message_.sentence, message_.detail,
                                           window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void DriveRow::browse() {
    QStringList patterns;
    for (const QString& extension : extensions_ ? extensions_() : QStringList{}) {
        patterns << QStringLiteral("*.") + extension;
    }
    const QString filter = patterns.isEmpty()
                               ? tr("All files (*)")
                               : tr("Disc images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
    const QString recent = settings_ && !settings_->recent_discs().isEmpty()
                               ? QFileInfo(settings_->recent_discs().front()).absolutePath()
                               : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Insert a Disc in Drive %1").arg(number_), recent, filter);
    if (!path.isEmpty()) {
        clear_message();
        discs_.insert(number_, path);
    }
}

void DriveRow::rebuild_menu() {
    menu_->clear();
    menu_->addAction(tr("Browse..."), this, &DriveRow::browse);
    QMenu* recent = menu_->addMenu(tr("Recent"));
    const QStringList paths = settings_ ? settings_->recent_discs() : QStringList{};
    const QStringList labels = recent_disc_labels(paths);
    for (int i = 0; i < paths.size(); ++i) {
        const QString path = paths.at(i);
        QAction* action = recent->addAction(labels.at(i), this, [this, path] {
            clear_message();
            discs_.insert(number_, path);
        });
        action->setToolTip(path);
        // A disc since moved or deleted is shown, not hidden, but cannot be
        // chosen.
        action->setEnabled(QFileInfo::exists(path));
    }
    if (paths.isEmpty()) {
        recent->addAction(tr("No recent discs"))->setEnabled(false);
    } else {
        recent->addSeparator();
        recent->addAction(tr("Clear Recent Discs"), this, [this] {
            if (settings_) {
                settings_->clear_discs();
            }
        });
    }
    menu_->setToolTipsVisible(true);
    recent->setToolTipsVisible(true);
    QMenu* blank = menu_->addMenu(tr("New Blank Disc"));
    for (const BlankDiscTemplate& disc : discs_.blank_discs()) {
        blank->addAction(disc.label, this, [this, disc] { new_blank_disc(disc); });
    }
    blank->setEnabled(!discs_.blank_discs().empty());
}

void DriveRow::new_blank_disc(const BlankDiscTemplate& blank) {
    // The server knows the format from the extension: only the template's
    // own is offered, and added if the name lacks it (9.3).
    const QString extension = blank.extension();
    const QString suggested = QDir(QDir::homePath()).filePath(blank.label + extension);
    QString path = QFileDialog::getSaveFileName(
        this, tr("New Blank Disc"), suggested,
        tr("%1 disc image (*%2)").arg(extension.mid(1).toUpper(), extension));
    if (path.isEmpty()) {
        return;
    }
    if (!path.toLower().endsWith(extension)) {
        path += extension;
    }
    clear_message();
    Q_EMIT blank_disc_requested(number_, blank.label, path);
}

QString DriveRow::drop_refusal() const {
    if (!(shares_ && shares_())) {
        return tr("Server is on another host");
    }
    const DriveInfo* info = drive();
    if (info && info->state != DriveInfo::State::empty) {
        return tr("Eject disc first");
    }
    return {};
}

void DriveRow::drop_files(const QStringList& paths) {
    // The state may have changed during the drag.
    const QString refusal = drop_refusal();
    if (!refusal.isEmpty()) {
        show_notice(refusal);
        return;
    }
    if (paths.size() != 1) {
        show_notice(tr("Drop a single disc image"));
        return;
    }
    clear_message();
    discs_.insert(number_, paths.front());
}

void DriveRow::set_highlighted(bool highlighted) {
    highlighted_ = highlighted;
    setBackgroundRole(highlighted ? QPalette::Highlight : QPalette::Window);
    setForegroundRole(highlighted ? QPalette::HighlightedText : QPalette::WindowText);
}

void DriveRow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    // Judged on what is known without the file: the drive and the server.
    const QString refusal = drop_refusal();
    if (!refusal.isEmpty()) {
        show_notice(refusal);
        event->ignore();
        return;
    }
    // A fresh, acceptable drag clears what an earlier one left.
    if (message_.kind != DriveMessage::Kind::none) {
        clear_message();
    }
    set_highlighted(true);
    event->acceptProposedAction();
}

void DriveRow::dragLeaveEvent(QDragLeaveEvent* event) {
    set_highlighted(false);
    QWidget::dragLeaveEvent(event);
}

void DriveRow::dropEvent(QDropEvent* event) {
    set_highlighted(false);
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    event->acceptProposedAction();
    drop_files(paths);
}

void DriveRow::contextMenuEvent(QContextMenuEvent* event) {
    const DriveInfo* info = drive();
    if (!info || info->state == DriveInfo::State::empty || info->disc_url.isEmpty()) {
        return;
    }
    const QString path = path_for_disc_url(info->disc_url);
    QMenu menu(this);
    // Worth quoting wherever the server is.
    menu.addAction(tr("Copy Path"), this,
                   [path] { QApplication::clipboard()->setText(path); });
    if (shares_ && shares_()) {
        menu.addAction(tr("Show in File Manager"), this, [path] { show_in_file_manager(path); });
    }
    menu.exec(event->globalPos());
}

StorageSidebar::StorageSidebar(SessionDiscs& discs, ClientSettings* settings,
                               DriveRow::Shares shares, QWidget* parent)
    : QWidget(parent), discs_(discs), settings_(settings), shares_(std::move(shares)),
      cli_(std::make_unique<CoreCli>()) {
    setObjectName(QStringLiteral("storageSidebar"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto* content = new QWidget(scroll);
    scroll->setWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 8);
    status_ = new QLabel(content);
    status_->setAlignment(Qt::AlignCenter);
    QFont bold = status_->font();
    bold.setBold(true);
    status_->setFont(bold);
    status_detail_ = new QLabel(content);
    status_detail_->setAlignment(Qt::AlignCenter);
    status_detail_->setWordWrap(true);
    status_detail_->setEnabled(false);
    layout->addWidget(status_);
    layout->addWidget(status_detail_);
    rows_layout_ = new QVBoxLayout;
    rows_layout_->setSpacing(0);
    layout->addLayout(rows_layout_);
    layout->addStretch(1);

    connect(&discs_, &SessionDiscs::loaded_changed, this, &StorageSidebar::rebuild);
    connect(&discs_, &SessionDiscs::drives_changed, this, [this] {
        if (rows_.size() != discs_.drives().size()) {
            rebuild();
            return;
        }
        for (DriveRow* row : rows_) {
            row->refresh();
        }
    });
    connect(&discs_, &SessionDiscs::insert_finished, this,
            [this](int drive, const QString& path, const DiscOutcome& outcome) {
                if (outcome.ok) {
                    // A recent disc is one the server took (9.2).
                    if (settings_) {
                        settings_->remember_disc(path);
                    }
                    return;
                }
                if (DriveRow* target = row(drive)) {
                    const QString marker = disc_error_marker(outcome.kind);
                    target->show_failure(
                        marker.isEmpty() ? tr("Insert failed") : marker,
                        tr("Beebium could not insert %1 in drive %2.")
                            .arg(QFileInfo(path).fileName())
                            .arg(drive),
                        outcome.message);
                }
            });
    connect(&discs_, &SessionDiscs::eject_finished, this,
            [this](int drive, const DiscOutcome& outcome) {
                if (DriveRow* target = row(drive); target && !outcome.ok) {
                    const QString marker = disc_error_marker(outcome.kind);
                    target->show_failure(marker.isEmpty() ? tr("Eject failed") : marker,
                                         tr("The disc in drive %1 could not be ejected.").arg(drive),
                                         outcome.message);
                }
            });
    connect(&discs_, &SessionDiscs::cancel_finished, this,
            [this](int drive, const DiscOutcome& outcome) {
                if (DriveRow* target = row(drive); target && !outcome.ok) {
                    const QString marker = disc_error_marker(outcome.kind);
                    target->show_failure(marker.isEmpty() ? tr("Couldn't cancel eject") : marker,
                                         tr("The eject in drive %1 could not be cancelled.").arg(drive),
                                         outcome.message);
                }
            });
    connect(&discs_, &SessionDiscs::create_finished, this,
            [this](const QString&, const DiscOutcome& outcome) {
                // The row that asked for it.
                DriveRow* target = pending_blank_drive_ ? row(*pending_blank_drive_) : nullptr;
                pending_blank_drive_.reset();
                if (!target) {
                    return;
                }
                if (!outcome.ok) {
                    const QString marker = create_disc_error_marker(outcome.kind);
                    target->show_failure(marker.isEmpty() ? tr("Couldn't create disc") : marker,
                                         create_disc_error_sentence(outcome.kind),
                                         outcome.message);
                    return;
                }
                // Then the second step: insert what was made, which enters
                // the recents as any insert does.
                discs_.insert(target->number(), outcome.created_path);
            });
    rebuild();
}

StorageSidebar::~StorageSidebar() = default;

void StorageSidebar::make_blank_disc(int drive, const QString& label, const QString& path) {
    pending_blank_drive_ = drive;
    discs_.create_disc(label, path);
}

DriveRow* StorageSidebar::row(int number) const {
    for (DriveRow* candidate : rows_) {
        if (candidate->number() == number) {
            return candidate;
        }
    }
    return nullptr;
}

QString StorageSidebar::status_text() const {
    return status_->isHidden() ? QString() : status_->text();
}

void StorageSidebar::rebuild() {
    for (DriveRow* row : rows_) {
        delete row;
    }
    rows_.clear();
    status_->show();
    status_detail_->hide();
    if (!discs_.loaded()) {
        status_->setText(tr("Loading..."));
        return;
    }
    if (!discs_.has_controller() || discs_.drives().empty()) {
        status_->setText(tr("No Storage Devices"));
        status_detail_->setText(tr("This machine has no disc controller."));
        status_detail_->show();
        return;
    }
    status_->hide();
    load_formats();
    for (const DriveInfo& drive : discs_.drives()) {
        auto* row = new DriveRow(discs_, drive.number, settings_, shares_,
                                 [this] { return extensions_; }, this);
        connect(row, &DriveRow::blank_disc_requested, this, &StorageSidebar::make_blank_disc);
        rows_layout_->addWidget(row);
        rows_.push_back(row);
    }
}

void StorageSidebar::load_formats() {
    // Once, from this host's servers: Browse is only offered for a server
    // sharing this filesystem, so a local server's list is the right one.
    // Any model's server lists the same formats, so model-b's is asked.
    if (formats_asked_) {
        return;
    }
    const auto servers =
        locate_servers_on_this_host(settings_ ? settings_->servers_dirpath() : QString());
    if (!servers) {
        return;  // an unfiltered picker rather than a stale list
    }
    formats_asked_ = true;
    QPointer<StorageSidebar> guard(this);
    cli_->run(servers->executable_filepath(QStringLiteral("model-b")),
              {QStringLiteral("list-disc-formats")}, [this, guard](const CliResult& result) {
                  if (!guard) {
                      return;
                  }
                  if (result.ok()) {
                      extensions_ = disc_extensions(parse_disc_formats(result.standard_output),
                                                    QStringLiteral("floppy"));
                  } else {
                      formats_asked_ = false;  // asked again next time
                  }
              });
}

}  // namespace beebium::linux_client

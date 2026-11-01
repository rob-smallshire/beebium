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

#include "app/PictureDrop.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QPalette>
#include <QStringList>
#include <QUrl>
#include <QWidget>

#include <algorithm>

namespace beebium::linux_client {

namespace {

bool is_empty(const DriveInfo& drive) {
    return drive.state == DriveInfo::State::empty;
}

QStringList local_files(const QMimeData* data) {
    QStringList paths;
    for (const QUrl& url : data->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    return paths;
}

}  // namespace

PictureDrop picture_drop(const std::vector<DriveInfo>& drives, bool shares_filesystem,
                         int file_count) {
    PictureDrop drop;
    if (!shares_filesystem) {
        drop.refusal = QObject::tr("Server is on another host");
        return drop;
    }
    if (drives.empty()) {
        drop.refusal = QObject::tr("This machine has no disc drives");
        return drop;
    }
    const auto drive_0 = std::find_if(drives.begin(), drives.end(),
                                      [](const DriveInfo& d) { return d.number == 0; });
    const auto empties = std::count_if(drives.begin(), drives.end(), is_empty);
    if (drive_0 != drives.end() && is_empty(*drive_0)) {
        drop.drive = 0;
    } else if (empties == 1) {
        drop.drive = std::find_if(drives.begin(), drives.end(), is_empty)->number;
    } else if (empties == 0) {
        drop.refusal = QObject::tr("Eject a disc first");
        return drop;
    } else {
        drop.refusal = QObject::tr("Drop onto a drive in the Storage sidebar");
        return drop;
    }
    if (file_count != 1) {
        drop.drive.reset();
        drop.refusal = QObject::tr("Drop a single disc image");
    }
    return drop;
}

PictureDropTarget::PictureDropTarget(QWidget* picture, Drives drives, Shares shares,
                                     Insert insert)
    : QObject(picture),
      picture_(picture),
      drives_(std::move(drives)),
      shares_(std::move(shares)),
      insert_(std::move(insert)),
      notice_(new QLabel(picture)) {
    notice_->setObjectName(QStringLiteral("pictureDropNotice"));
    notice_->setAutoFillBackground(true);
    notice_->setMargin(8);
    notice_->hide();
    hide_.setSingleShot(true);
    connect(&hide_, &QTimer::timeout, this, &PictureDropTarget::hide_notice);
    picture_->setAcceptDrops(true);
    picture_->installEventFilter(this);
}

QString PictureDropTarget::notice_text() const {
    return notice_->isVisibleTo(picture_) ? notice_->text() : QString();
}

bool PictureDropTarget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != picture_) {
        return false;
    }
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        if (!drag->mimeData()->hasUrls()) {
            drag->ignore();
            return true;
        }
        // The file count is judged at the drop, as a drive row does.
        const PictureDrop drop = picture_drop(drives_(), shares_());
        if (drop.drive) {
            show_notice(tr("Drop to insert into drive %1").arg(*drop.drive), true);
            drag->acceptProposedAction();
        } else {
            show_notice(drop.refusal, false);
            drag->ignore();
        }
        return true;
    }
    case QEvent::DragLeave:
        // A refusal stays to be read; a promise is withdrawn.
        if (!hide_.isActive()) {
            hide_notice();
        }
        return true;
    case QEvent::Drop: {
        auto* drop_event = static_cast<QDropEvent*>(event);
        const QStringList paths = local_files(drop_event->mimeData());
        const PictureDrop drop =
            picture_drop(drives_(), shares_(), static_cast<int>(paths.size()));
        drop_event->acceptProposedAction();
        if (!drop.drive) {
            show_notice(drop.refusal, false);
            return true;
        }
        hide_notice();
        insert_(*drop.drive, paths.front());
        return true;
    }
    default:
        return false;
    }
}

void PictureDropTarget::show_notice(const QString& text, bool accepted) {
    notice_->setText(text);
    QPalette palette = notice_->palette();
    const QPalette& source = picture_->palette();
    palette.setColor(QPalette::Window,
                     source.color(accepted ? QPalette::Highlight : QPalette::ToolTipBase));
    palette.setColor(QPalette::WindowText,
                     source.color(accepted ? QPalette::HighlightedText : QPalette::ToolTipText));
    notice_->setPalette(palette);
    notice_->adjustSize();
    notice_->move((picture_->width() - notice_->width()) / 2, 16);
    notice_->raise();
    notice_->show();
    if (accepted) {
        hide_.stop();
    } else {
        hide_.start(REFUSAL_SHOWN_MS);
    }
}

void PictureDropTarget::hide_notice() {
    hide_.stop();
    notice_->hide();
}

}  // namespace beebium::linux_client

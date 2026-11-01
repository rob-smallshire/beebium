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

#ifndef BEEBIUM_LINUX_APP_PICTURE_DROP_HPP
#define BEEBIUM_LINUX_APP_PICTURE_DROP_HPP

#include "app/SessionDiscs.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QWidget;

namespace beebium::linux_client {

// Where a disc image dropped onto the machine's picture goes: drive 0 if it
// is empty, where a BBC user puts a disc to boot it; else the only empty
// drive; else nowhere, with the reason. A Linux addition: the macOS client
// takes drops on its drive rows only, and this keeps their rules (client
// specification 9.2): judged on the drives and the server's location alone,
// never on the file, which is the server's to judge on insert; refused up
// front when the server does not share this filesystem (3.2); one file only.
struct PictureDrop {
    std::optional<int> drive;
    QString refusal;  // empty when there is a drive
};

PictureDrop picture_drop(const std::vector<DriveInfo>& drives, bool shares_filesystem,
                         int file_count = 1);

// The picture as a drop target: while a drag of files hovers, a notice says
// where the disc will go, in the highlight colour, or why it cannot, with
// the forbidden cursor; a refusal stays a moment after the drag leaves. A
// drop judges again, the drives having perhaps changed meanwhile.
class PictureDropTarget : public QObject {
    Q_OBJECT

public:
    using Drives = std::function<std::vector<DriveInfo>()>;
    using Shares = std::function<bool()>;
    using Insert = std::function<void(int drive, const QString& path)>;

    static constexpr int REFUSAL_SHOWN_MS = 2500;

    PictureDropTarget(QWidget* picture, Drives drives, Shares shares, Insert insert);

    // What the notice says, while it shows.
    QString notice_text() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void show_notice(const QString& text, bool accepted);
    void hide_notice();

    QWidget* picture_;
    Drives drives_;
    Shares shares_;
    Insert insert_;
    QLabel* notice_;
    QTimer hide_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_PICTURE_DROP_HPP

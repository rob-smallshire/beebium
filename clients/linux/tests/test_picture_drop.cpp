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

// A disc image dropped onto the machine's picture (a Linux addition; the
// macOS client takes drops on its drive rows only): drive 0 if it is empty,
// where a BBC user puts a disc to boot it, else the only empty drive, else
// refused with a reason; judged on the drives and the server's location
// alone, as a drive row's drop is (client specification 9.2, 3.2).

#include "app/PictureDrop.hpp"

#include "WaitUntil.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QMimeData>
#include <QUrl>
#include <QWidget>

using namespace beebium::linux_client;

namespace {

std::vector<DriveInfo> drives(std::initializer_list<bool> loaded) {
    std::vector<DriveInfo> result;
    int number = 0;
    for (bool is_loaded : loaded) {
        DriveInfo drive;
        drive.number = number++;
        drive.state = is_loaded ? DriveInfo::State::loaded : DriveInfo::State::empty;
        result.push_back(drive);
    }
    return result;
}

}  // namespace

TEST_CASE("A drop on the picture goes into drive 0 when it is empty", "[storage][drop]") {
    const PictureDrop drop = picture_drop(drives({false, false}), true);
    REQUIRE(drop.drive.has_value());
    CHECK(*drop.drive == 0);
    CHECK(drop.refusal.isEmpty());
}

TEST_CASE("With drive 0 full, a drop goes into the only empty drive", "[storage][drop]") {
    const PictureDrop drop = picture_drop(drives({true, false}), true);
    REQUIRE(drop.drive.has_value());
    CHECK(*drop.drive == 1);
}

TEST_CASE("With drive 0 full and several drives empty, the drop names no drive: refused",
          "[storage][drop]") {
    const PictureDrop drop = picture_drop(drives({true, false, false}), true);
    CHECK_FALSE(drop.drive.has_value());
    CHECK(drop.refusal == QStringLiteral("Drop onto a drive in the Storage sidebar"));
}

TEST_CASE("With every drive full, a drop is refused", "[storage][drop]") {
    const PictureDrop drop = picture_drop(drives({true, true}), true);
    CHECK_FALSE(drop.drive.has_value());
    CHECK(drop.refusal == QStringLiteral("Eject a disc first"));
}

TEST_CASE("A server on another host refuses a drop before anything else", "[storage][drop]") {
    const PictureDrop drop = picture_drop(drives({false, false}), false);
    CHECK_FALSE(drop.drive.has_value());
    CHECK(drop.refusal == QStringLiteral("Server is on another host"));
}

TEST_CASE("A machine with no drives refuses a drop", "[storage][drop]") {
    const PictureDrop drop = picture_drop({}, true);
    CHECK_FALSE(drop.drive.has_value());
    CHECK(drop.refusal == QStringLiteral("This machine has no disc drives"));
}

TEST_CASE("More than one file is refused at the drop", "[storage][drop]") {
    CHECK(picture_drop(drives({false}), true, 2).refusal ==
          QStringLiteral("Drop a single disc image"));
    CHECK(picture_drop(drives({false}), true, 1).drive == 0);
}

namespace {

struct Target {
    QWidget picture;
    std::vector<DriveInfo> state = drives({true, false});
    bool shares = true;
    std::vector<std::pair<int, QString>> inserted;
    PictureDropTarget target{&picture, [this] { return state; }, [this] { return shares; },
                             [this](int drive, const QString& path) {
                                 inserted.emplace_back(drive, path);
                             }};

    QMimeData* files(QStringList paths) {
        auto* data = new QMimeData;
        QList<QUrl> urls;
        for (const QString& path : paths) {
            urls << QUrl::fromLocalFile(path);
        }
        data->setUrls(urls);
        return data;
    }
};

}  // namespace

TEST_CASE("Dragging over the picture says where the disc will go, and dropping inserts it",
          "[storage][drop]") {
    Target t;
    std::unique_ptr<QMimeData> data(t.files({QStringLiteral("/discs/Elite.ssd")}));
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&t.picture, &enter);
    CHECK(enter.isAccepted());
    CHECK(t.target.notice_text() == QStringLiteral("Drop to insert into drive 1"));
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&t.picture, &drop);
    REQUIRE(t.inserted.size() == 1);
    CHECK(t.inserted.front().first == 1);
    CHECK(t.inserted.front().second == QStringLiteral("/discs/Elite.ssd"));
}

TEST_CASE("A refused drag is forbidden and says why", "[storage][drop]") {
    Target t;
    t.state = drives({true, true});
    std::unique_ptr<QMimeData> data(t.files({QStringLiteral("/discs/Elite.ssd")}));
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&t.picture, &enter);
    CHECK_FALSE(enter.isAccepted());
    CHECK(t.target.notice_text() == QStringLiteral("Eject a disc first"));
    CHECK(t.inserted.empty());
}

TEST_CASE("A drop re-judges the drives, which may have changed during the drag",
          "[storage][drop]") {
    Target t;
    std::unique_ptr<QMimeData> data(t.files({QStringLiteral("/discs/Elite.ssd")}));
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&t.picture, &enter);
    t.state = drives({true, true});
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, data.get(), Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&t.picture, &drop);
    CHECK(t.inserted.empty());
    CHECK(t.target.notice_text() == QStringLiteral("Eject a disc first"));
}

TEST_CASE("Something other than files is not for the picture", "[storage][drop]") {
    Target t;
    QMimeData text;
    text.setText(QStringLiteral("PRINT"));
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&t.picture, &enter);
    CHECK_FALSE(enter.isAccepted());
    CHECK(t.target.notice_text().isEmpty());
}

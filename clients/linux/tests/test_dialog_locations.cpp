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

// Where a file dialog starts (client specification 3.6).

#include "app/ClientSettings.hpp"
#include "app/DialogLocations.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

using namespace beebium::linux_client;

namespace {

// A scratch world: the settings file, a Documents folder, the server's
// per-user tree and a few folders of the user's own.
struct World {
    QTemporaryDir dir;
    ClientSettings settings{dir.filePath(QStringLiteral("Beebium.conf"))};
    DialogLocations::Rules rules;
    World() {
        REQUIRE(dir.isValid());
        rules.documents_dirpath = folder(QStringLiteral("Documents"));
        rules.excluded_dirpaths = {folder(QStringLiteral("state"))};
    }
    QString folder(const QString& relative) {
        const QString dirpath = dir.filePath(relative);
        REQUIRE(QDir().mkpath(dirpath));
        return dirpath;
    }
    QString file(const QString& relative) {
        const QString filepath = dir.filePath(relative);
        REQUIRE(QDir().mkpath(QFileInfo(filepath).absolutePath()));
        QFile f(filepath);
        REQUIRE(f.open(QIODevice::WriteOnly));
        return filepath;
    }
    DialogLocations locations() { return DialogLocations(&settings, rules); }
};

constexpr auto OPEN = DialogDirection::open;
constexpr auto SAVE = DialogDirection::save;

}  // namespace

TEST_CASE("A dialog used for the first time starts in Documents", "[dialogs]") {
    World w;
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-tape"), OPEN) == w.rules.documents_dirpath);
    CHECK(w.locations().start_dirpath(QStringLiteral("new-blank-tape"), SAVE) == w.rules.documents_dirpath);
}

TEST_CASE("Each dialog function remembers its own folder, open and save apart", "[dialogs]") {
    World w;
    const QString collection = w.folder(QStringLiteral("collection"));
    w.locations().remember_file(QStringLiteral("insert-tape"), OPEN, w.file(QStringLiteral("collection/Elite.uef")));
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-tape"), OPEN) == collection);
    // Not the save side of the same function, and not another function.
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-tape"), SAVE) == w.rules.documents_dirpath);
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-disc"), OPEN) == w.rules.documents_dirpath);
    const QString work = w.folder(QStringLiteral("work"));
    w.locations().remember_file(QStringLiteral("new-blank-tape"), SAVE, QDir(work).filePath(QStringLiteral("Mine.uef")));
    CHECK(w.locations().start_dirpath(QStringLiteral("new-blank-tape"), SAVE) == work);
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-tape"), OPEN) == collection);
    // Kept as a client setting, across runs.
    ClientSettings reopened(w.dir.filePath(QStringLiteral("Beebium.conf")));
    CHECK(DialogLocations(&reopened, w.rules).start_dirpath(QStringLiteral("insert-tape"), OPEN) == collection);
}

TEST_CASE("A save dialog proposes its name in the folder it starts in", "[dialogs]") {
    World w;
    const QString work = w.folder(QStringLiteral("work"));
    const QString name = QStringLiteral("Blank C15 (15 minutes).uef");
    CHECK(w.locations().start_filepath(QStringLiteral("new-blank-tape"), name) ==
          QDir(w.rules.documents_dirpath).filePath(name));
    w.locations().remember_file(QStringLiteral("new-blank-tape"), SAVE, QDir(work).filePath(QStringLiteral("x.uef")));
    CHECK(w.locations().start_filepath(QStringLiteral("new-blank-tape"), name) == QDir(work).filePath(name));
}

TEST_CASE("A folder chooser remembers the folder chosen, or the one holding a chosen item",
          "[dialogs]") {
    World w;
    const QString destination = w.folder(QStringLiteral("machines/archive"));
    w.locations().remember_folder(QStringLiteral("move-machine"), SAVE, destination);
    CHECK(w.locations().start_dirpath(QStringLiteral("move-machine"), SAVE) == destination);
    // A machine is a folder, chosen as an item: its folder is remembered.
    const QString machine = w.folder(QStringLiteral("machines/Elite.beebium-machine"));
    w.locations().remember_file(QStringLiteral("open-machine"), OPEN, machine);
    CHECK(w.locations().start_dirpath(QStringLiteral("open-machine"), OPEN) == w.dir.filePath(QStringLiteral("machines")));
}

TEST_CASE("A remembered folder since removed falls back to Documents", "[dialogs]") {
    World w;
    const QString gone = w.folder(QStringLiteral("gone"));
    w.locations().remember_file(QStringLiteral("new-blank-tape"), SAVE, QDir(gone).filePath(QStringLiteral("a.uef")));
    REQUIRE(w.locations().start_dirpath(QStringLiteral("new-blank-tape"), SAVE) == gone);
    REQUIRE(QDir(gone).removeRecursively());
    CHECK(w.locations().start_dirpath(QStringLiteral("new-blank-tape"), SAVE) == w.rules.documents_dirpath);
}

TEST_CASE("A remembered folder no longer writable is not where a save starts, but still an open",
          "[dialogs]") {
    if (geteuid() == 0) {
        SKIP("Run as root, every folder is writable.");
    }
    World w;
    const QString locked = w.folder(QStringLiteral("locked"));
    w.locations().remember_file(QStringLiteral("save-machine"), SAVE, QDir(locked).filePath(QStringLiteral("M")));
    w.locations().remember_file(QStringLiteral("insert-disc"), OPEN, QDir(locked).filePath(QStringLiteral("d.ssd")));
    REQUIRE(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    CHECK(w.locations().start_dirpath(QStringLiteral("save-machine"), SAVE) == w.rules.documents_dirpath);
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-disc"), OPEN) == locked);
    QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

TEST_CASE("A file chosen among the server's templates or state is honoured but not remembered",
          "[dialogs]") {
    World w;
    const QString collection = w.folder(QStringLiteral("collection"));
    w.locations().remember_file(QStringLiteral("insert-tape"), OPEN, w.file(QStringLiteral("collection/Elite.uef")));
    for (const QString& chosen :
         {w.file(QStringLiteral("server/blank-tapes/Blank C15 (15 minutes).uef")),
          w.file(QStringLiteral("share/beebium/blank-discs/Blank 80T.ssd")),
          w.file(QStringLiteral("beebium/tapes/blank/Blank C15 (15 minutes).uef")),  // a source tree's
          w.file(QStringLiteral("beebium/discs/blank/Blank 80T.ssd")),
          w.file(QStringLiteral("state/tapes/Mine.uef")),  // the server's per-user tree
          w.file(QStringLiteral("state/machines/x/discs/a.ssd"))}) {
        CAPTURE(chosen.toStdString());
        CHECK(w.locations().excluded(QFileInfo(chosen).absolutePath()));
        w.locations().remember_file(QStringLiteral("insert-tape"), OPEN, chosen);
        CHECK(w.locations().start_dirpath(QStringLiteral("insert-tape"), OPEN) == collection);
    }
    // A folder of the user's merely named like one is the user's.
    CHECK_FALSE(w.locations().excluded(w.folder(QStringLiteral("blank"))));
    CHECK_FALSE(w.locations().excluded(w.folder(QStringLiteral("my-blank-tapes"))));
}

TEST_CASE("A template or state folder remembered before is never where a dialog starts",
          "[dialogs]") {
    World w;
    const QString templates = w.folder(QStringLiteral("server/blank-tapes"));
    w.settings.set_dialog_dirpath(QStringLiteral("new-blank-tape/save"), templates);
    CHECK(w.locations().start_dirpath(QStringLiteral("new-blank-tape"), SAVE) == w.rules.documents_dirpath);
    w.settings.set_dialog_dirpath(QStringLiteral("insert-tape/open"), w.folder(QStringLiteral("state/tapes")));
    CHECK(w.locations().start_dirpath(QStringLiteral("insert-tape"), OPEN) == w.rules.documents_dirpath);
}

TEST_CASE("Without settings a dialog starts in Documents and remembers nothing", "[dialogs]") {
    World w;
    DialogLocations none(nullptr, w.rules);
    none.remember_file(QStringLiteral("insert-tape"), OPEN, w.file(QStringLiteral("collection/Elite.uef")));
    CHECK(none.start_dirpath(QStringLiteral("insert-tape"), OPEN) == w.rules.documents_dirpath);
}

TEST_CASE("The host's rules: its Documents folder, and the server's per-user tree excluded",
          "[dialogs]") {
    QTemporaryDir dir;
    const QByteArray saved = qgetenv("BEEBIUM_USER_DIRPATH");
    qputenv("BEEBIUM_USER_DIRPATH", dir.filePath(QStringLiteral("elsewhere")).toLocal8Bit());
    const DialogLocations::Rules rules = DialogLocations::host_rules();
    if (saved.isNull()) {
        qunsetenv("BEEBIUM_USER_DIRPATH");
    } else {
        qputenv("BEEBIUM_USER_DIRPATH", saved);
    }
    CHECK(rules.documents_dirpath == QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    CHECK(rules.excluded_dirpaths.contains(dir.filePath(QStringLiteral("elsewhere"))));
    // Never the home folder as Documents' stand-in by the client's choice.
    CHECK(rules.documents_dirpath != QString());
}

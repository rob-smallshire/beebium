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

#include "app/DialogLocations.hpp"

#include "app/ClientSettings.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <utility>

namespace beebium::linux_client {

namespace {

// One setting a function and direction: the function's name kept to
// letters, digits, '-' and '_', so a parameter's pointer is one key.
QString key_of(const QString& function, DialogDirection direction) {
    QString name = function;
    for (QChar& c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) {
            c = QLatin1Char('_');
        }
    }
    return name + (direction == DialogDirection::open ? QStringLiteral("/open")
                                                      : QStringLiteral("/save"));
}

// The path as compared: absolute and clean, and with links resolved where
// it exists.
QString resolved(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

bool within(const QString& dirpath, const QString& outer) {
    const QString inside = resolved(dirpath);
    const QString root = resolved(outer);
    return inside == root || inside.startsWith(root + QLatin1Char('/'));
}

}  // namespace

DialogLocations::Rules DialogLocations::host_rules() {
    Rules rules;
    rules.documents_dirpath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    // The server's per-user tree, found as it finds it.
    const QString user = qEnvironmentVariable("BEEBIUM_USER_DIRPATH");
    if (!user.isEmpty()) {
        rules.excluded_dirpaths << user;
    } else {
        const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
        rules.excluded_dirpaths << QDir(config.isEmpty() ? QDir::home().filePath(QStringLiteral(".config"))
                                                         : config)
                                       .filePath(QStringLiteral("beebium"));
    }
    return rules;
}

DialogLocations::DialogLocations(ClientSettings* settings)
    : DialogLocations(settings, host_rules()) {}

DialogLocations::DialogLocations(ClientSettings* settings, Rules rules)
    : settings_(settings), rules_(std::move(rules)) {}

bool DialogLocations::excluded(const QString& dirpath) const {
    for (const QString& outer : rules_.excluded_dirpaths) {
        if (within(dirpath, outer)) {
            return true;
        }
    }
    // The server's templates, wherever its build or installation keeps
    // them, and a source tree's own.
    const QStringList parts = resolved(dirpath).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (parts[i] == QStringLiteral("blank-discs") || parts[i] == QStringLiteral("blank-tapes")) {
            return true;
        }
        if (i > 0 && parts[i] == QStringLiteral("blank") &&
            (parts[i - 1] == QStringLiteral("discs") || parts[i - 1] == QStringLiteral("tapes"))) {
            return true;
        }
    }
    return false;
}

QString DialogLocations::start_dirpath(const QString& function, DialogDirection direction) const {
    if (settings_) {
        const QString remembered = settings_->dialog_dirpath(key_of(function, direction));
        const QFileInfo info(remembered);
        if (!remembered.isEmpty() && info.isDir() && !excluded(remembered) &&
            (direction == DialogDirection::open || info.isWritable())) {
            return remembered;
        }
    }
    return rules_.documents_dirpath;
}

QString DialogLocations::start_filepath(const QString& function, const QString& proposed_name) const {
    return QDir(start_dirpath(function, DialogDirection::save)).filePath(proposed_name);
}

void DialogLocations::remember_file(const QString& function, DialogDirection direction,
                                    const QString& chosen_path) {
    remember(function, direction, QFileInfo(chosen_path).absolutePath());
}

void DialogLocations::remember_folder(const QString& function, DialogDirection direction,
                                      const QString& chosen_dirpath) {
    remember(function, direction, QDir::cleanPath(QFileInfo(chosen_dirpath).absoluteFilePath()));
}

void DialogLocations::remember(const QString& function, DialogDirection direction,
                               const QString& dirpath) {
    if (settings_ && !dirpath.isEmpty() && !excluded(dirpath)) {
        settings_->set_dialog_dirpath(key_of(function, direction), dirpath);
    }
}

QString open_file(QWidget* parent, DialogLocations locations, const QString& function,
                  const QString& title, const QString& filter) {
    const QString chosen = QFileDialog::getOpenFileName(
        parent, title, locations.start_dirpath(function, DialogDirection::open), filter);
    if (!chosen.isEmpty()) {
        locations.remember_file(function, DialogDirection::open, chosen);
    }
    return chosen;
}

QString save_file(QWidget* parent, DialogLocations locations, const QString& function,
                  const QString& title, const QString& proposed_name, const QString& filter,
                  QFileDialog::Options options) {
    const QString chosen = QFileDialog::getSaveFileName(
        parent, title, locations.start_filepath(function, proposed_name), filter, nullptr, options);
    if (!chosen.isEmpty()) {
        locations.remember_file(function, DialogDirection::save, chosen);
    }
    return chosen;
}

QString choose_directory(QWidget* parent, DialogLocations locations, const QString& function,
                         DialogDirection direction, const QString& title, bool item) {
    const QString chosen = QFileDialog::getExistingDirectory(
        parent, title, locations.start_dirpath(function, direction), QFileDialog::ShowDirsOnly);
    if (!chosen.isEmpty()) {
        if (item) {
            locations.remember_file(function, direction, chosen);
        } else {
            locations.remember_folder(function, direction, chosen);
        }
    }
    return chosen;
}

}  // namespace beebium::linux_client

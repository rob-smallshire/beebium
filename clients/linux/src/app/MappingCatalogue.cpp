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

#include "app/MappingCatalogue.hpp"

#include "app/KeyboardInput.hpp"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace beebium::linux_client {

const std::vector<QString>& built_in_mapping_names() {
    static const std::vector<QString> names{
        QStringLiteral("Logical"), QStringLiteral("Arcadians"), QStringLiteral("Boffin"),
        QStringLiteral("Planetoid"), QStringLiteral("Snapper"), QStringLiteral("Thrust"),
    };
    return names;
}

QString user_mappings_dirpath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("Beebium/keyboard-mappings"));
}

std::vector<MappingChoice> mapping_choices(const QString& user_dirpath) {
    std::vector<MappingChoice> choices;
    for (const QString& name : built_in_mapping_names()) {
        MappingChoice choice;
        choice.name = name;
        choice.built_in = true;
        choice.mapping = built_in_mapping(name);
        if (choice.mapping) {
            choice.id = QString::fromStdString(choice.mapping->id);
            choice.name = QString::fromStdString(choice.mapping->name);
        }
        choices.push_back(choice);
    }
    const QDir directory(user_dirpath);
    for (const QFileInfo& file :
         directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        MappingChoice choice;
        choice.filepath = file.absoluteFilePath();
        choice.name = file.completeBaseName();
        QFile reading(choice.filepath);
        if (!reading.open(QIODevice::ReadOnly)) {
            choice.problem = QObject::tr("It could not be read: %1").arg(reading.errorString());
            choices.push_back(choice);
            continue;
        }
        const auto result = parse_mapping_file(reading.readAll().toStdString());
        if (const auto* error = std::get_if<MappingFileError>(&result)) {
            choice.problem = QString::fromStdString(error->reason);
        } else {
            choice.mapping = std::get<MappingFile>(result);
            choice.id = QString::fromStdString(choice.mapping->id);
            if (!choice.mapping->name.empty()) {
                choice.name = QString::fromStdString(choice.mapping->name);
            }
        }
        choices.push_back(choice);
    }
    return choices;
}

namespace {

QString key_name(const QString& code) {
    static const QHash<QString, QString> names{
        {QStringLiteral("Backspace"), QStringLiteral("Backspace")},
        {QStringLiteral("Enter"), QStringLiteral("Enter")},
        {QStringLiteral("Escape"), QStringLiteral("Esc")},
        {QStringLiteral("Tab"), QStringLiteral("Tab")},
        {QStringLiteral("Space"), QStringLiteral("Space")},
        {QStringLiteral("CapsLock"), QStringLiteral("Caps Lock")},
        {QStringLiteral("ShiftLeft"), QStringLiteral("Left Shift")},
        {QStringLiteral("ShiftRight"), QStringLiteral("Right Shift")},
        {QStringLiteral("ControlLeft"), QStringLiteral("Left Ctrl")},
        {QStringLiteral("ControlRight"), QStringLiteral("Right Ctrl")},
        {QStringLiteral("AltLeft"), QStringLiteral("Left Alt")},
        {QStringLiteral("AltRight"), QStringLiteral("Right Alt")},
        {QStringLiteral("MetaLeft"), QStringLiteral("Left Meta")},
        {QStringLiteral("MetaRight"), QStringLiteral("Right Meta")},
        {QStringLiteral("Backquote"), QStringLiteral("`")},
        {QStringLiteral("Minus"), QStringLiteral("-")},
        {QStringLiteral("Equal"), QStringLiteral("=")},
        {QStringLiteral("BracketLeft"), QStringLiteral("[")},
        {QStringLiteral("BracketRight"), QStringLiteral("]")},
        {QStringLiteral("Backslash"), QStringLiteral("\\")},
        {QStringLiteral("IntlBackslash"), QStringLiteral("\\ (ISO)")},
        {QStringLiteral("Semicolon"), QStringLiteral(";")},
        {QStringLiteral("Quote"), QStringLiteral("'")},
        {QStringLiteral("Comma"), QStringLiteral(",")},
        {QStringLiteral("Period"), QStringLiteral(".")},
        {QStringLiteral("Slash"), QStringLiteral("/")},
        {QStringLiteral("ArrowUp"), QStringLiteral("Up")},
        {QStringLiteral("ArrowDown"), QStringLiteral("Down")},
        {QStringLiteral("ArrowLeft"), QStringLiteral("Left")},
        {QStringLiteral("ArrowRight"), QStringLiteral("Right")},
        {QStringLiteral("PageUp"), QStringLiteral("Page Up")},
        {QStringLiteral("PageDown"), QStringLiteral("Page Down")},
    };
    if (const auto found = names.constFind(code); found != names.cend()) {
        return *found;
    }
    static const QRegularExpression letter(QStringLiteral("^Key([A-Z])$"));
    static const QRegularExpression digit(QStringLiteral("^Digit([0-9])$"));
    static const QRegularExpression numpad(QStringLiteral("^Numpad(.+)$"));
    if (const auto match = letter.match(code); match.hasMatch()) {
        return match.captured(1);
    }
    if (const auto match = digit.match(code); match.hasMatch()) {
        return match.captured(1);
    }
    if (const auto match = numpad.match(code); match.hasMatch()) {
        return QObject::tr("Keypad %1").arg(match.captured(1));
    }
    return code;  // F1 to F24, Home, End, Insert, Delete as they are
}

}  // namespace

QString host_key_label(const MappingEntry& entry) {
    QStringList parts;
    // KDE's order.
    if (entry.with & MODIFIER_CONTROL) {
        parts << QObject::tr("Ctrl");
    }
    if (entry.with & MODIFIER_ALT) {
        parts << QObject::tr("Alt");
    }
    if (entry.with & MODIFIER_SHIFT) {
        parts << QObject::tr("Shift");
    }
    if (entry.with & MODIFIER_META) {
        parts << QObject::tr("Meta");
    }
    if (entry.code) {
        parts << key_name(QString::fromStdString(*entry.code));
    } else if (entry.linux_scancode) {
        parts << QObject::tr("key %1").arg(*entry.linux_scancode);
    } else {
        return {};  // another platform's key: not this host's to name
    }
    return parts.join(QLatin1Char('+'));
}

std::vector<ReferenceRow> mapping_reference(const MappingFile& mapping) {
    std::vector<ReferenceRow> rows;
    for (const MappingEntry& entry : mapping.keys) {
        if (!entry.action || entry.action->empty()) {
            continue;
        }
        const QString host = host_key_label(entry);
        if (host.isEmpty()) {
            continue;
        }
        rows.push_back({host, QString::fromStdString(*entry.action)});
    }
    return rows;
}

}  // namespace beebium::linux_client

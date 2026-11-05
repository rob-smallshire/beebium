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

#include "app/configuration/SectionWidgets.hpp"

#include <QPalette>

#include <algorithm>
#include <QUrl>

namespace beebium::linux_client {

QLabel* section_heading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    return label;
}

QLabel* section_caption(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);
    return label;
}

DiagnosticLine::DiagnosticLine(QWidget* parent) : QLabel(parent) {
    setWordWrap(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse);
    hide();
}

void DiagnosticLine::show_for(const std::vector<PresetDiagnostic>& diagnostics,
                              const std::string& pointer) {
    QStringList lines;
    bool error = false;
    for (const auto& d : diagnostics) {
        if (d.path == pointer || d.path.rfind(pointer + "/", 0) == 0) {
            lines << QString::fromStdString(d.message);
            error = error || d.error();
        }
    }
    QPalette palette = this->palette();
    palette.setColor(QPalette::WindowText,
                     error ? QColor(0xd0, 0x3b, 0x3b) : QColor(0xd9, 0x7a, 0x00));
    setPalette(palette);
    setText(lines.join(QLatin1Char('\n')));
    setVisible(!lines.isEmpty());
}

std::vector<std::string> point_extensions(const std::optional<PresetSchema>& schema,
                                          const std::string& point) {
    if (schema) {
        for (const auto& p : schema->attachment_points()) {
            if (p.id == point) {
                return p.extensions;
            }
        }
    }
    return {};
}

bool names_one_of(const nlohmann::ordered_json& entry, const std::vector<std::string>& names) {
    const std::string name = entry.is_object() ? entry.value("name", std::string()) : std::string();
    return std::find(names.begin(), names.end(), name) != names.end();
}

QString path_of_uri(const std::string& uri) {
    const QUrl url(QString::fromStdString(uri));
    return url.isLocalFile() ? url.toLocalFile() : QString::fromStdString(uri);
}

std::string uri_of_path(const QString& path) {
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toStdString();
}

}  // namespace beebium::linux_client

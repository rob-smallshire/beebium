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

#ifndef BEEBIUM_LINUX_APP_CONFIGURATION_SECTION_WIDGETS_HPP
#define BEEBIUM_LINUX_APP_CONFIGURATION_SECTION_WIDGETS_HPP

#include "preset/PresetResolution.hpp"
#include "preset/PresetSchema.hpp"

#include <optional>

#include <QLabel>
#include <QScrollArea>
#include <QString>

class QPushButton;

#include <string>
#include <vector>

namespace beebium::linux_client {

// What the configuration sections share: a heading, a secondary caption,
// and the server's diagnostics shown beneath what they are about.

QLabel* section_heading(const QString& text, QWidget* parent);
QLabel* section_caption(QWidget* parent);

// A row or group another section sent the user to: scrolled into view and
// tinted with the highlight colour for a moment.
void mark_revealed(QWidget* widget);
// A section's content, scrolled down only: at least as wide as the content
// needs, so its minimum width reaches the dialog and nothing is cut off at
// the sides however narrow the dialog is made.
class VerticalScrollArea : public QScrollArea {
public:
    explicit VerticalScrollArea(QWidget* parent);
    void set_content(QWidget* content);
    QSize minimumSizeHint() const override;
    bool eventFilter(QObject* watched, QEvent* event) override;
};

// A flat, link-like button that takes the user to another section.
QPushButton* section_link(const QString& text, QWidget* parent);

// Beneath a control: the diagnostics at a JSON pointer or within it, in
// the server's words, coloured by severity; hidden when there are none.
class DiagnosticLine : public QLabel {
public:
    explicit DiagnosticLine(QWidget* parent);
    void show_for(const std::vector<PresetDiagnostic>& diagnostics, const std::string& pointer);
};

// A file:// URI's path, else the reference as written (a bundled name).
// The extensions a schema's point takes, for judging a section's part of
// the draft's extensions.
std::vector<std::string> point_extensions(const std::optional<PresetSchema>& schema,
                                          const std::string& point);
// Whether an extension entry names one of the extensions given.
bool names_one_of(const nlohmann::ordered_json& entry, const std::vector<std::string>& names);

QString path_of_uri(const std::string& uri);
std::string uri_of_path(const QString& path);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_CONFIGURATION_SECTION_WIDGETS_HPP

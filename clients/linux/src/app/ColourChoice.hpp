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

#ifndef BEEBIUM_LINUX_APP_COLOUR_CHOICE_HPP
#define BEEBIUM_LINUX_APP_COLOUR_CHOICE_HPP

#include "display/VideoSettings.hpp"

#include <QColor>
#include <QObject>

#include <functional>
#include <optional>

class QToolButton;
class QWidget;

namespace beebium::linux_client {

// A "Reset to default" button, to sit beside the value it resets; the
// caller enables it while that value differs from its default.
QToolButton* make_reset_button(QWidget* parent, const QString& object_name);

// An opaque colour the user picks, as the macOS client's resettable colour
// well: a swatch that opens the platform's colour picker, a reset beside it
// (placed by the caller, so rows can line up), and the same reset on the
// swatch's context menu. Named <object_name> and <object_name>Reset.
class ColourChoice : public QObject {
    Q_OBJECT

public:
    // Asks for a colour, titled; nothing when the user cancels.
    using Chooser =
        std::function<std::optional<QColor>(const QColor& current, const QString& title)>;

    ColourChoice(const QString& object_name, const QString& title, Rgb fallback, QWidget* parent);

    QToolButton* well() const { return well_; }
    QToolButton* reset() const { return reset_; }

    Rgb colour() const { return colour_; }
    // Show this colour; not a choice.
    void set_colour(Rgb colour);
    // The platform's colour picker by default.
    void set_chooser(Chooser chooser) { chooser_ = std::move(chooser); }

Q_SIGNALS:
    // The user chose a colour, or reset it.
    void chosen(beebium::linux_client::Rgb colour);

private:
    void choose(Rgb colour);

    QString title_;
    Rgb fallback_;
    Rgb colour_;
    Chooser chooser_;
    QToolButton* well_;
    QToolButton* reset_;
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_COLOUR_CHOICE_HPP
